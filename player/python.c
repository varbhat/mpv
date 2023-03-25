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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>

#include <Python.h>

#include "boolobject.h"
#include "object.h"
#include "osdep/io.h"

#include "mpv_talloc.h"

#include "common/common.h"
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
    char *last_error_str;
    struct stats_ctx *stats;
} PyScriptCtx;

static PyTypeObject PyScriptCtx_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "script_ctx",
    .tp_basicsize = sizeof(PyScriptCtx),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "script_ctx object",
};




// mpv python module
static struct PyModuleDef mpv_module_def = {
    PyModuleDef_HEAD_INIT,
    "mpv",
    NULL,
    -1,
    NULL, NULL, NULL, NULL, NULL
};


// Main Entrypoint
static int s_load_python(struct mp_script_args *args)
{
    int r = -1;

    PyScriptCtx *ctx = PyObject_New(PyScriptCtx, &PyScriptCtx_Type);
    ctx->client = args->client;
    ctx->mpctx = args->mpctx;
    ctx->log = args->log;
    ctx->last_error_str = talloc_strdup(ctx, "Cannot initialize Python");
    ctx->filename = args->filename;
    ctx->path = args->path;
    ctx->stats = stats_ctx_create(ctx, args->mpctx->global,
                    mp_tprintf(80, "script/%s", mpv_client_name(args->client)));


    stats_register_thread_cputime(ctx->stats, "cpu");

    Py_Initialize();


    ctx->mpv_module = PyModule_Create(&mpv_module_def);


    if (Py_FinalizeEx() < 0) {
        goto error_out;
    }

    r = 0;

error_out:
    if (r)
        MP_FATAL(ctx, "%s\n", ctx->last_error_str);
    if (Py_IsInitialized())
        Py_Finalize();

    Py_TYPE(ctx)->tp_free((PyObject*)ctx);
    return r;
}



/**********************************************************************
 *  Main mp.* scripting APIs and helpers
 *********************************************************************/

// dummy function template
static PyObject* script_dummy(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;

    if (!PyArg_ParseTuple(args, "s", &p))
        return NULL;


    int res = 0; // do work
    if(res < 0) {
        PyErr_SetString(PyExc_Exception, mpv_error_string(res));
        return NULL;
    }

    Py_RETURN_NONE;
}

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
        PyErr_SetString(PyExc_TypeError, "Argument must be a list of strings");
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
    if(res < 0) {
        PyErr_SetString(PyExc_Exception, mpv_error_string(res));
        return NULL;
    }

    Py_RETURN_NONE;
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

    if(res < 0) {
        PyErr_SetString(PyExc_Exception, mpv_error_string(res));
        return NULL;
    }

    Py_RETURN_NONE;
}





static PyObject* script_del_property(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;

    if (!PyArg_ParseTuple(args, "s", &p))
        return NULL;


    int res = mpv_del_property(ctx->client, p); // do work
    if(res < 0) {
        PyErr_SetString(PyExc_Exception, mpv_error_string(res));
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject* script_set_property(PyObject* self, PyObject* args)
{
    PyScriptCtx *ctx= (PyScriptCtx *)self;

    const char *p;
    const char *v;

    if (!PyArg_ParseTuple(args, "ss", &p, &v))
        return NULL;


    int res = 0; // do work
    if(res < 0) {
        PyErr_SetString(PyExc_Exception, mpv_error_string(res));
        return NULL;
    }

    Py_RETURN_NONE;
}


// main export of this file, used by cplayer to load js scripts
const struct mp_scripting mp_scripting_py = {
    .name = "python",
    .file_ext = "py",
    .load = s_load_python,
};