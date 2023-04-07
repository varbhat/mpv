"""
The python wrapper module for the embedded and extended functionalities
"""

import mpv


class Mpv:
    """

    This class wraps the mpv client (/libmpv) API hooks defined in the
    embedded/extended python. See: player/python.c

    """

    def extension_ok(self) -> bool:
        return mpv.extension_ok()


mp = MP = Mpv()
print(mp.extension_ok())
