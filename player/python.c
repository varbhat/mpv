/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// #include <assert.h>
// #include <stdio.h>
// #include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>

#include "boolobject.h"
#include "longobject.h"
#include "object.h"
#include "osdep/io.h"

#include "mpv_talloc.h"

#include "common/common.h"
#include "common/global.h"
#include "options/m_property.h"
#include "common/msg.h"
#include "common/msg_control.h"
#include "common/stats.h"
#include "options/m_option.h"
#include "input/input.h"
#include "options/path.h"
#include "misc/bstr.h"
#include "misc/json.h"
#include "osdep/subprocess.h"
#include "osdep/timer.h"
#include "osdep/threads.h"
#include "pyerrors.h"
#include "stream/stream.h"
#include "sub/osd.h"
#include "core.h"
#include "command.h"
#include "client.h"
#include "libmpv/client.h"
#include "ta/ta_talloc.h"


// List of builtin modules and their contents as strings.
// All these are generated from player/python/*.py
static const char *const builtin_files[][3] = {
    {"@/defaults.py",
#   include "generated/player/python/defaults.py.inc"
    },
    {0}
};


// Represents a loaded script. Each has its own Python state.
typedef struct {
    PyObject_HEAD

    const char *filename;
    const char *path; // NULL if single file
    struct mpv_handle *client;
    struct MPContext *mpctx;
    struct mp_log *log;
    PyObject* mpv_module;
    struct stats_ctx *stats;
} PyScriptCtx;


static PyTypeObject PyScriptCtx_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "script_ctx",
    .tp_basicsize = sizeof(PyScriptCtx),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "script_ctx object",
};


// module and type def
/* ========================================================================== */

static PyObject *MpvError;

typedef struct {
    PyObject_HEAD
    PyObject        *pympv_attr;
} PyMpvObject;

static PyTypeObject PyMpv_Type;

#define PyCtxObject_Check(v)      Py_IS_TYPE(v, &PyMpv_Type)

static PyMpvObject *
newPyMpvObject(PyObject *args)
{
    PyMpvObject *self;
    self = PyObject_New(PyMpvObject, &PyMpv_Type);
    if (self == NULL)
        return NULL;
    self->pympv_attr = NULL;
    return self;
}


static void
PyMpv_dealloc(PyMpvObject *self)
{
    Py_XDECREF(self->pympv_attr);
    PyObject_Free(self);
}


static PyObject *
setup(PyObject *self, PyObject *args)
{
    return Py_NewRef(Py_NotImplemented);
}


static PyMethodDef PyMpv_methods[] = {
    {"setup", (PyCFunction)setup, METH_VARARGS,
     PyDoc_STR("Just a test method to see if extending is working.")},
    {NULL, NULL, 0, NULL}                                                 /* Sentinal */
};


static PyObject *
PyMpv_getattro(PyMpvObject *self, PyObject *name)
{
    if (self->pympv_attr != NULL) {
        PyObject *v = PyDict_GetItemWithError(self->pympv_attr, name);
        if (v != NULL) {
            return Py_NewRef(v);
        }
        else if (PyErr_Occurred()) {
            return NULL;
        }
    }
    return PyObject_GenericGetAttr((PyObject *)self, name);
}


static int
PyMpv_setattr(PyMpvObject *self, const char *name, PyObject *v)
{
    if (self->pympv_attr == NULL) {
        self->pympv_attr = PyDict_New();
        if (self->pympv_attr == NULL)
            return -1;
    }
    if (v == NULL) {
        int rv = PyDict_DelItemString(self->pympv_attr, name);
        if (rv < 0 && PyErr_ExceptionMatches(PyExc_KeyError))
            PyErr_SetString(PyExc_AttributeError,
                "delete non-existing PyMpv attribute");
        return rv;
    }
    else
        return PyDict_SetItemString(self->pympv_attr, name, v);
}


static PyTypeObject PyMpv_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "mpv.MPV",
    .tp_basicsize = sizeof(PyMpvObject),
    .tp_dealloc = (destructor)PyMpv_dealloc,
    .tp_getattr = (getattrfunc)0,
    .tp_setattr = (setattrfunc)PyMpv_setattr,
    .tp_getattro = (getattrofunc)PyMpv_getattro,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = PyMpv_methods,
};

static PyObject *
mpv_extension_ok(PyObject *self, PyObject *args)
{
    return Py_NewRef(Py_True);
}


static PyMethodDef Mpv_methods[] = {
    {"extension_ok", (PyCFunction)mpv_extension_ok, METH_VARARGS,             /* METH_VARARGS | METH_KEYWORDS (PyObject *self, PyObject *args, PyObject **kwargs) */
     PyDoc_STR("Just a test method to see if extending is working.")},
    {NULL, NULL, 0, NULL}                                                     /* Sentinal */
};


static int
pympv_exec(PyObject *m)
{
    if (PyType_Ready(&PyMpv_Type) < 0)
        return -1;

    if (MpvError == NULL) {
        MpvError = PyErr_NewException("mpv.error", NULL, NULL);
        if (MpvError == NULL)
            return -1;
    }
    int rc = PyModule_AddType(m, (PyTypeObject *)MpvError);
    Py_DECREF(MpvError);
    if (rc < 0)
        return -1;

    return 0;
}


static struct PyModuleDef_Slot pympv_slots[] = {
    {Py_mod_exec, pympv_exec},
    {0, NULL}
};


// mpv python module
static struct PyModuleDef mpv_module_def = {
    PyModuleDef_HEAD_INIT,
    "mpv",
    NULL,
    0,
    Mpv_methods,
    pympv_slots,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_mpv(void)
{
    return PyModuleDef_Init(&mpv_module_def);
}

/* ========================================================================== */

// Main Entrypoint (We want only one call here.)
static int s_load_python(struct mp_script_args *args)
{
    int r = -1;

    if (PyImport_AppendInittab("mpv", PyInit_mpv) == -1) {
        fprintf(stderr, "Error: could not extend in-built modules table\n");
        goto error_out;
    }

    Py_Initialize();

    PyObject *pympv = PyImport_ImportModule("mpv");

    PyObject *globals = PyDict_New();
    PyObject *locals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
    PyDict_SetItemString(globals, "mpv", pympv);

    // char *default_script = "generated/player/python/defaults.py.inc";
    // FILE *fp = fopen(default_script, "r");
    // PyRun_File(fp, PyUnicode_DecodeFSDefault(default_script), Py_file_input, globals, locals);
    // fclose(fp);

    PyObject *scripts = PyTuple_New(args->script_count);

    PyObject *os = PyImport_ImportModule("os");
    PyObject *path = PyObject_GetAttrString(os, "path");
    PyObject *exists = PyObject_GetAttrString(path, "exists");

    char **py_scripts = args->py_scripts;
    for (size_t i = 0; i < args->script_count; i++) {

        PyObject *file = PyUnicode_DecodeFSDefault(py_scripts[i]);
        PyObject *file_exist = PyObject_CallOneArg(exists, file);
        if (file_exist == Py_True) {
            FILE *fp = fopen(py_scripts[i], "r");
            PyRun_File(fp, py_scripts[i], Py_file_input, globals, locals);
            fclose(fp);
        }
        Py_DECREF(file);
        Py_DECREF(file_exist);
    }

    Py_DECREF(os);
    Py_DECREF(path);
    Py_DECREF(globals);
    Py_DECREF(locals);
    Py_DECREF(pympv);

    if (Py_FinalizeEx() < 0) {

        goto error_out;
    }

    r = 0;

    // PyScriptCtx *ctx = PyObject_New(PyScriptCtx, &PyScriptCtx_Type);
    // ctx->client = args->client;
    // ctx->mpctx = args->mpctx;
    // ctx->log = args->log;
    // ctx->filename = args->filename;
    // ctx->path = args->path;
    // ctx->stats = stats_ctx_create(ctx, args->mpctx->global,
    //                 mp_tprintf(80, "script/%s", mpv_client_name(args->client)));

    // stats_register_thread_cputime(ctx->stats, "cpu");

error_out:
    // PyThreadState_Swap(NULL);  // Switch back to the main interpreter

    // Py_EndInterpreter(sub_interp);  // Delete the sub-interpreter
    // if (r)
    //     MP_FATAL(ctx, "%s\n", "Python Initialization Error");
    if (Py_IsInitialized())
        Py_Finalize();
    // Py_TYPE(ctx)->tp_free((PyObject*)ctx);
    return r;
}



/**********************************************************************
 *  Main mp.* scripting APIs and helpers
 *********************************************************************/

 static PyObject* check_error(PyObject* self, int err)
{
    if (err >= 0) {
        Py_RETURN_TRUE;
    }
    PyErr_SetString(PyExc_Exception, mpv_error_string(err));
    Py_RETURN_NONE;
}

// static void makenode(PyObject *obj, struct mpv_node *node) {
//     if (obj == Py_None) {
//         node->format = MPV_FORMAT_NONE;
//         return;
//     }
//
//     if (PyBool_Check(obj)) {
//         node->format = MPV_FORMAT_FLAG;
//         node->u.flag = (int) PyObject_IsTrue(obj);
//         return;
//     }
//
//     if (PyLong_Check(obj)) {
//         node->format = MPV_FORMAT_INT64;
//         node->u.int64 = PyLong_AsLongLong(obj);
//         return;
//     }
//
//     if (PyFloat_Check(obj)) {
//         node->format = MPV_FORMAT_DOUBLE;
//         node->u.double_ = PyFloat_AsDouble(obj);
//         return;
//     }
//
//     if (PyUnicode_Check(obj)) {
//         node->format = MPV_FORMAT_STRING;
//         node->u.string = (char*) PyUnicode_AsUTF8(obj);
//         return;
//     }
//
//     if (PyList_Check(obj)) {
//         node->format = MPV_FORMAT_NODE_ARRAY;
//         node->u.list = mpv_node_array_alloc(PyList_Size(obj));
//         if (!node->u.list) {
//             PyErr_SetString(PyExc_RuntimeError, "Failed to allocate node array");
//             return;
//         }
//         for (int i = 0; i < PyList_Size(obj); i++) {
//             PyObject *item = PyList_GetItem(obj, i);
//             struct mpv_node *child = &node->u.list->values[i];
//             makenode_pyobj(item, child);
//         }
//         return;
//     }
//
//     if (PyDict_Check(obj)) {
//         node->format = MPV_FORMAT_NODE_MAP;
//         node->u.list = mpv_node_array_alloc(PyDict_Size(obj));
//         if (!node->u.list) {
//             PyErr_SetString(PyExc_RuntimeError, "Failed to allocate node array");
//             return;
//         }
//         Py_ssize_t pos = 0;
//         PyObject *key, *value;
//         while (PyDict_Next(obj, &pos, &key, &value)) {
//             struct mpv_node *child = &node->u.list->values[pos];
//             makenode_pyobj(key, child);
//             makenode_pyobj(value, child + 1);
//             pos++;
//         }
//         return;
//     }
//
//     PyErr_Format(PyExc_TypeError, "Unsupported object type: %s", Py_TYPE(obj)->tp_name);
// }



// dummy function template
static PyObject* script_dummy(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;

    if (!PyArg_ParseTuple(args, "s", &p))
        return NULL;


    int res = 0; // do work
    return check_error(self, res);
}

/**************************************************************************************************/
// args: log level, varargs
static PyObject* script_log(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    // Parse args to list_obj
    PyObject* list_obj;
    if (!PyArg_ParseTuple(args, "O", &list_obj)) {
        return NULL;
    }
    if (!PyList_Check(list_obj)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a list of strings");
        return NULL;
    }
    int length = PyList_Size(list_obj);

    if(length<1) {
        PyErr_SetString(PyExc_TypeError, "Insufficient Args");
        return NULL;
    }

    PyObject* log_obj = PyList_GetItem(list_obj, 0);
    if (!PyUnicode_Check(log_obj)) {
        PyErr_SetString(PyExc_TypeError, "List must contain only strings");
        return NULL;
    }

    int msgl = mp_msg_find_level(PyUnicode_AsUTF8(log_obj));
    if (msgl < 0) {
        PyErr_SetString(PyExc_TypeError,PyUnicode_AsUTF8(log_obj));
        return NULL;
    }

    if(length>1) {
        struct mp_log *log = ctx->log;
        for (Py_ssize_t i = 1; i < length; i++) {
            PyObject* str_obj = PyList_GetItem(list_obj, i);
            if (!PyUnicode_Check(str_obj)) {
                PyErr_SetString(PyExc_TypeError, "List must contain only strings");
                return NULL;
            }
            mp_msg(log, msgl, (i == 2 ? "%s" : " %s"), PyUnicode_AsUTF8(str_obj));
        }
        mp_msg(log, msgl, "\n");
    }

    Py_RETURN_NONE;
}

// args: string -> string
static PyObject* script_find_config_file(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *fname;

    if (!PyArg_ParseTuple(args, "s", &fname))
        return NULL;

    char *path = mp_find_config_file(NULL, ctx->mpctx->global, fname);
    if (path) {
        PyObject* ret =  PyUnicode_FromString(path);
        talloc_free(path);
        return ret;
    } else {
        talloc_free(path);
        PyErr_SetString(PyExc_FileNotFoundError, "Not found");
        return NULL;
    }

    Py_RETURN_NONE;
}

// args: string,bool
static PyObject* script_request_event(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *event;
    bool enable;

    if (!PyArg_ParseTuple(args, "sp", &event, &enable))
        return NULL;

    for (int n = 0; n < 256; n++) {
        // some n's may be missing ("holes"), returning NULL
        const char *name = mpv_event_name(n);
        if (name && strcmp(name, event) == 0) {
            if (mpv_request_event(ctx->client, n, enable) >= 0) {
                Py_RETURN_TRUE;
            } else {
                Py_RETURN_FALSE;
            }
            return NULL;
        }
    }

    Py_RETURN_NONE;
}


// args: string
static PyObject* script_enable_messagess(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *level;

    if (!PyArg_ParseTuple(args, "s", &level))
        return NULL;


    int res = mpv_request_log_messages(ctx->client, level);
    if (res == MPV_ERROR_INVALID_PARAMETER) {
        PyErr_SetString(PyExc_Exception, "Invalid Log Error");
        return NULL;
    }
    return check_error(self, res);
}

// args: string
static PyObject* script_command(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *s;

    if (!PyArg_ParseTuple(args, "s", &s))
        return NULL;

    int res = mpv_command_string(ctx->client, s);
    return check_error(self, res);
}

// args: list of strings
static PyObject* script_commandv(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    PyObject* list_obj;

    if (!PyArg_ParseTuple(args, "O", &list_obj)) {
        return NULL;
    }

    if (!PyList_Check(list_obj)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a list of strings");
        return NULL;
    }

    int length = PyList_Size(list_obj);

    const char *arglist[50];

    if (length > MP_ARRAY_SIZE(arglist)) {
        PyErr_SetString(PyExc_TypeError, "Too many arguments");
        return NULL;
    }


    for (Py_ssize_t i = 0; i < length; i++) {
        PyObject* str_obj = PyList_GetItem(list_obj, i);
        if (!PyUnicode_Check(str_obj)) {
            PyErr_SetString(PyExc_TypeError, "List must contain only strings");
            return NULL;
        }
        arglist[i] = PyUnicode_AsUTF8(str_obj);
    }

    arglist[length] = NULL;

    int res = mpv_command(ctx->client, arglist);
    return check_error(self, res);
}

// args: two strings
static PyObject* script_set_property(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;
    const char *v;

    if (!PyArg_ParseTuple(args, "ss", &p, &v))
        return NULL;

    int res = mpv_set_property_string(ctx->client, p, v);
    return check_error(self, res);
}

// args: string
static PyObject* script_del_property(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;

    if (!PyArg_ParseTuple(args, "s", &p))
        return NULL;


    int res = mpv_del_property(ctx->client, p);
    return check_error(self, res);
}

static PyObject* script_enable_messages(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *level;

    if (!PyArg_ParseTuple(args, "s", &level))
        return NULL;


    int r = mpv_request_log_messages(ctx->client, level);
    if (r == MPV_ERROR_INVALID_PARAMETER) {
        PyErr_SetString(PyExc_Exception, "Invalid log level");
        Py_RETURN_NONE;
    }
    return check_error(self, r);
}

// args: name, native value
// static PyObject* script_set_property_native(PyObject* self, PyObject* args)
// {
//     //TODO
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     const char *p;
//
//     if (!PyArg_ParseTuple(args, "s", &p))
//         return NULL;
//
//
//     int res = 0; // do work
//     return check_error(self, res);
// }

// args: string,bool
// static PyObject* script_set_property_bool(PyObject* self, PyObject* args)
// {
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     const char *p;
//     bool v;
//
//     if (!PyArg_ParseTuple(args, "sp", &p, &v))
//         return NULL;
//
//     int res = mpv_set_property(ctx->client, p, MPV_FORMAT_FLAG, &v);
//     return check_error(self, res);
// }

// args: name
// static PyObject* script_get_property_number(PyObject* self, PyObject* args)
// {
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     double result;
//     const char* name;
//
//     if (!PyArg_ParseTuple(args, "s", &name))
//         return NULL;
//
//     int err = mpv_get_property(ctx->client, name, MPV_FORMAT_DOUBLE, &result);
//     if(err >= 0) {
//         return PyLong_FromDouble(result);
//     } else {
//         //TODO
//         PyErr_SetString(PyExc_Exception, mpv_error_string(err));
//         Py_RETURN_NONE;
//     }
// }


// args: name
// static PyObject* script_get_property_bool(PyObject* self, PyObject* args)
// {
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     const char *name;
//
//     if (!PyArg_ParseTuple(args, "s", &name))
//         return NULL;
//
//     int result = 0;
//     int err = mpv_get_property(ctx->client, name, MPV_FORMAT_FLAG, &result);
//     if (err >= 0) {
//         bool ret = !!result;
//         if(ret) {
//             Py_RETURN_TRUE;
//         } else {
//             Py_RETURN_FALSE;
//         }
//     }
//
//     return check_error(self, res);
// }

// static PyObject* script_set_property_native(PyObject* self, PyObject* args, int is_osd)
// {
//
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     const char *name;
//     PyObject *py_node;
//
//     if (!PyArg_ParseTuple(args, "sO", &p, &py_node))
//         return NULL;
//
//     char *result = NULL;
//     int err = mpv_get_property(ctx->client, name, MPV_FORMAT_STRING, &result);
//     if (err >= 0) {
//         return PyUnicode_FromString(result);
//     }
//     return check_error(self, err);
// }


// static PyObject* script_get_property(PyObject* self, PyObject* args, int is_osd)
// {
//     PyScriptCtx *ctx= (PyScriptCtx *)self;
//
//     const char *name;
//
//     if (!PyArg_ParseTuple(args, "s", &p))
//         return NULL;
//
//     char *result = NULL;
//     int err = mpv_get_property(ctx->client, name, MPV_FORMAT_STRING, &result);
//     if (err >= 0) {
//         return PyUnicode_FromString(result);
//     }
//     return check_error(self, err);
// }


/************************************************************************************************/

// main export of this file, used by cplayer to load js scripts
const struct mp_scripting mp_scripting_py = {
    .name = "python",
    .file_ext = "py",
    .load = s_load_python,
    .no_thread = true,
};
