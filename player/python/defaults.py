"""
The python wrapper module for the embedded and extended functionalities
"""

import logging
import mpv
import threading
import time

MPV_EVENT_WAIT_TIMEOUT = 1


class MpvHandler(logging.StreamHandler):

    def emit(self, record):
        lname = record.levelname.lower()
        if lname == 'warning':
            lname = 'warn'
        if lname == 'critical':
            lname = 'fatal'
        mpv.handle_log([lname, str(record.msg)]);


logging.root.addHandler(MpvHandler())
logging.root.setLevel(logging.DEBUG)

from enum import IntEnum

class mpv_event_id(IntEnum):
    MPV_EVENT_NONE = 0
    MPV_EVENT_SHUTDOWN = 1
    MPV_EVENT_LOG_MESSAGE = 2
    MPV_EVENT_GET_PROPERTY_REPLY = 3
    MPV_EVENT_SET_PROPERTY_REPLY = 4
    MPV_EVENT_COMMAND_REPLY = 5
    MPV_EVENT_START_FILE = 6
    MPV_EVENT_END_FILE = 7
    MPV_EVENT_FILE_LOADED = 8
    MPV_EVENT_CLIENT_MESSAGE = 16
    MPV_EVENT_VIDEO_RECONFIG = 17
    MPV_EVENT_AUDIO_RECONFIG = 18
    MPV_EVENT_SEEK = 20
    MPV_EVENT_PLAYBACK_RESTART = 21
    MPV_EVENT_PROPERTY_CHANGE = 22
    MPV_EVENT_QUEUE_OVERFLOW = 24
    MPV_EVENT_HOOK = 25


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

    def handle_event(self, event_id: int) -> bool:
        """
        Returns:
            boolean specifing whether some event loop breaking
            condition has been satisfied.
        """
        if event_id == mpv_event_id.MPV_EVENT_SHUTDOWN:
            logging.warning(f"mpv event id: {event_id}")
            return True
        return False

    def wait_events(self):
        while True:
            if self.handle_event(mpv.wait_event(0)):
                break
            time.sleep(MPV_EVENT_WAIT_TIMEOUT)

    def run_in_separate_thread(self):
        self.thread = True
        threading.Thread(target=self.run).start()

    def run(self):
        mpv.create_stats()
        self.wait_events()
        self.shutdown()

    def shutdown(self):
        if self.thread:
            main_thread = threading.main_thread()
            main_thread.join()
        mpv.shutdown()

mpv.mpv = Mpv()
print(mpv.extension_ok())
