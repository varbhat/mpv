"""
The python wrapper module for the embedded and extended functionalities
"""

import mpv as _mpv
import sys
import threading
from pathlib import Path

__all__ = ['client_name', 'mpv']

client_name = Path(_mpv.filename).stem


class Registry:
    script_message = {}
    binds = {}


registry = Registry()


class Mpv:
    """

    This class wraps the mpv client (/libmpv) API hooks defined in the
    embedded/extended python. See: player/python.c

    """

    MPV_EVENT_CLIENT_MESSAGE = 16

    MPV_FORMAT_STRING = 1
    MPV_FORMAT_OSD_STRING = 2
    MPV_FORMAT_FLAG = 3
    MPV_FORMAT_INT64 = 4
    MPV_FORMAT_DOUBLE = 5
    MPV_FORMAT_NODE = 6

    observe_properties = {}

    def _log(self, level, *args):
        if not args:
            return
        msg = ' '.join([str(msg) for msg in args])
        _mpv.handle_log(level, f"({client_name}) {msg}\n")

    def info(self, *args):
        self._log("info", *args)

    def debug(self, *args):
        self._log("debug", *args)

    def warn(self, *args):
        self._log("warn", *args)

    def error(self, *a):
        self._log("error", *a)

    def fatal(self, *a):
        self._log("fatal", *a)

    def osd_message(self, text, duration=None, level=None):
        args = [text]
        args.append(duration if duration is not None else -1)
        if level is not None:
            args.append(level)
        self.commandv("show-text", *args)

    @property
    def name(self):
        return client_name

    threads = True

    def read_script(self, filename):
        file_path = Path(filename).resolve()
        if file_path.is_dir():
            file_path = file_path / "main.py"
        with file_path.open("r") as f:
            return str(file_path), f.read()

    def compile_script(self, filename):
        file_path, source = self.read_script(filename)
        return file_path, compile(source, file_path, "exec")

    def extension_ok(self) -> bool:
        return _mpv.extension_ok()

    def process_event(self, event_id, data):
        self.debug(f"received event: {event_id}, {data}")
        if event_id == self.MPV_EVENT_CLIENT_MESSAGE:
            c_name, cb_name = data[1].split('___')
            if c_name != self.name:
                return
            if data[2][0] == 'u':
                self.debug(f"calling callback {cb_name}")
                try:
                    registry.script_message[cb_name]()
                except:
                    self.printEx(0)
                self.debug(f"invoked {cb_name}")

    def command_string(self, name):
        return _mpv.command_string(name)

    def commandv(self, name, *args):
        return _mpv.commandv(name, *args)

    def command(self, node):
        """
        :type node: can be any data structure that resembles to mpv_node; can be a list of such nodes.
        """
        return _mpv.command(node)

    def find_config_file(self, filename):
        return _mpv.find_config_file(filename)

    def request_event(self, name, enable):
        return _mpv.request_event(name, enable)

    def enable_messages(self, level):
        return _mpv.enable_messages(level)

    def observe_property(self, property_name, mpv_format, reply_userdata=0):
        self.observe_properties[self.name + "___" + property_name] = {
            "reply_userdata": reply_userdata, "mpv_format": mpv_format}

    def _set_property(self, property_name, mpv_format, data):
        """
        :param str name: name of the property.

        """
        if not (type(property_name) == str and mpv_format in range(1, 7)):
            self.error("TODO: have a pointer to doc string")
            return
        return _mpv.set_property(property_name, mpv_format, data)

    def set_property_string(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_STRING, str(data))

    def set_property_osd(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_OSD_STRING, str(data))

    def set_property_bool(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_FLAG, 1 if bool(data) else 0)

    def set_property_int(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_INT64, int(data))

    def set_property_float(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_DOUBLE, float(data))

    def set_property_node(self, name, data):
        return self._set_property(name, mpv.MPV_FORMAT_NODE, data)

    def del_property(self, name):
        return _mpv.del_property(name)

    def get_property(self, property_name, mpv_format):
        if not (type(property_name) == str and mpv_format in range(1, 7)):
            self.error("TODO: have a pointer to doc string")
            return
        return _mpv.get_property(property_name, mpv_format)

    def get_property_string(self, name):
        return self.get_property(name, mpv.MPV_FORMAT_STRING)

    def get_property_osd(self, name):
        return self.get_property(name, mpv.MPV_FORMAT_OSD_STRING)

    def get_property_bool(self, name):
        return bool(self.get_property(name, mpv.MPV_FORMAT_FLAG))

    def get_property_int(self, name):
        return self.get_property(name, mpv.MPV_FORMAT_INT64)

    def get_property_float(self, name):
        return self.get_property(name, mpv.MPV_FORMAT_DOUBLE)

    def get_property_node(self, name):
        return self.get_property(name, mpv.MPV_FORMAT_NODE)

    def mpv_input_define_section(self, name, location, contents, builtin, owner):
        self.debug(f"define_section args:", name, location, contents, builtin, owner)
        return _mpv.mpv_input_define_section(name, location, contents, builtin, owner)

    # If a key binding is not defined in the current section, do not search the
    # other sections for it (like the default section). Instead, an unbound
    # key warning will be printed.
    MP_INPUT_EXCLUSIVE = 1
    # Prefer it to other sections.
    MP_INPUT_ON_TOP = 2
    # Let mp_input_test_dragging() return true, even if inside the mouse area.
    MP_INPUT_ALLOW_VO_DRAGGING = 4
    # Don't force mouse pointer visible, even if inside the mouse area.
    MP_INPUT_ALLOW_HIDE_CURSOR = 8

    def mpv_input_enable_section(self, name, flags):
        """
        Args:
            flags: bitwise flags from the values self.MP_INPUT_*
                    `or` (|) them to pass multiple flags.
        """
        return _mpv.mpv_input_enable_section(name, flags)

    def set_key_bindings_input_section(self):
        location = f"py_{client_name}_bs"

        builtin_binds = "\n".join(sorted(
            [binding['input'] for binding in registry.binds.values() \
                if binding['builtin'] and binding.get('input')]))
        if builtin_binds:
            name = f"py_{client_name}_kbs_builtin"
            self.mpv_input_define_section(name, location, "\n" + builtin_binds, True, client_name)
            self.mpv_input_enable_section(name, self.MP_INPUT_ON_TOP)

        reg_binds = "\n".join(sorted(
            [binding['input'] for binding in registry.binds.values() \
                if not binding['builtin'] and binding.get('input')]))
        if reg_binds:
            name = f"py_{client_name}_kbs"
            self.mpv_input_define_section(name, location, "\n" + reg_binds, False, client_name)
            self.mpv_input_enable_section(name, self.MP_INPUT_ON_TOP)

    def set_input_sections(self):
        self.set_key_bindings_input_section()

    def flush(self):
        self.debug(f"flushing {client_name}")
        # self.enable_client_message()
        self.set_input_sections()

    next_bid = 1

    def add_binding(self, key=None, name=None, builtin=False, **opts):
        """
        Args:
            opts: boolean memebers (repeatable, complex)
            builtin: whether to put the binding in the builtin section;
                        this means if the user defines bindings
                        using "{name}", they won't be ignored or overwritten - instead,
                        they are preferred to the bindings defined with this call
        """
        # copied from defaults.js (not sure what e and emit is yet)
        self.debug(f"loading binding {key}")
        key_data = opts
        self.next_bid += 1
        key_data.update(id=self.next_bid, builtin=builtin)
        if name is None:
            name = f"__keybinding{self.next_bid}"  # unique name

        def decorate(fn):
            registry.script_message[name] = fn

            def key_cb(state):
                # mpv.debug(state)
                # emit = state[1] == "m" if e == "u" else e == "d"
                # if (emit or e == "p" or e == "r") and key_data.get("repeatable", False):
                #     fn()
                fn()
            key_data['cb'] = key_cb

        if key is not None:
            key_data["input"] = key + " script-binding python/" + client_name + "___" + name
        registry.binds[name] = key_data

        return decorate

    def has_binding(self):
        return bool(registry.binds)

    def enable_client_message(self):
        if registry.binds:
            self.debug("enabling client message")
            if self.request_event(self.MPV_EVENT_CLIENT_MESSAGE, 1):
                self.debug("client-message enabled")
            else:
                self.debug("failed to enable client-message")

    def do_clean_up(self):
        raise NotImplementedError


mpv = Mpv()
