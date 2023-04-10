"""
The python wrapper module for the embedded and extended functionalities
"""

import mpv
from pathlib import Path

mpv.client_name = Path(globals()['filename']).stem


import logging


class MpvHandler(logging.StreamHandler):

    def emit(self, record):
        lname = record.levelname.lower()
        if lname == 'warning':
            lname = 'warn'
        if lname == 'critical':
            lname = 'fatal'
        empty_str = ''
        mpv.handle_log(
        [lname, f"({mpv.client_name if hasattr(mpv, 'client_name') else empty_str}) {record.msg}"])


for handler in logging.root.handlers:
    logging.root.removeHandler(handler)

logging.root.addHandler(MpvHandler())
logging.root.setLevel(logging.DEBUG)

logging.warning(f"initiating {mpv.client_name}")

class Mpv:
    """

    This class wraps the mpv client (/libmpv) API hooks defined in the
    embedded/extended python. See: player/python.c

    """

    thread: bool = False
    """
    Specifies whether the current script is running in a threaded context.
    """

    def extension_ok(self) -> bool:
        return mpv.extension_ok()

mpv.mpv = Mpv()
logging.warning(f"okay from extension: {mpv.extension_ok()}")
