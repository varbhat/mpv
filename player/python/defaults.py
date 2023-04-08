"""
The python wrapper module for the embedded and extended functionalities
"""
import asyncio
import logging
import mpv


class MpvHandler(logging.StreamHandler):

    def emit(self, record):
        mpv.handle_log(record);


logging.root.addHandler(MpvHandler())


class Mpv:
    """

    This class wraps the mpv client (/libmpv) API hooks defined in the
    embedded/extended python. See: player/python.c

    """

    def extension_ok(self) -> bool:
        return mpv.extension_ok()

    def run(self):
        pass

mpv.mpv = Mpv()
print(mpv.extension_ok())
