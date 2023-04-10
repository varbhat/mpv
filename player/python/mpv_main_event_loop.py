import logging

import mpvmainloop

import time
MPV_EVENT_WAIT_TIMEOUT = 1

from enum import IntEnum

logger = logging.getLogger('mainloop')


class MpvHandler(logging.StreamHandler):

    def emit(self, record):
        lname = record.levelname.lower()
        if lname == 'warning':
            lname = 'warn'
        if lname == 'critical':
            lname = 'fatal'
        empty_str = ''
        mpvmainloop.handle_log([lname, str(record.msg)])

logger.addHandler(MpvHandler())
logger.setLevel(logging.DEBUG)
logger.propagate = False

logger.info("logger set")

class events(IntEnum):
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


class MainLoop:
    clients: list[str] = []

    def __init__(self, clients):
        if clients is not None:
            self.clients = clients

    def get_client_index(self, client_name):
        if client_name not in self.clients:
            raise Exception
        return self.clients.index(client_name)

    def handle_event(self, event_id: int) -> bool:
        """
        Returns:
            boolean specifing whether some event loop breaking
            condition has been satisfied.
        """
        # if event_id == events.MPV_EVENT_SHUTDOWN:
        logger.warning(f"received event #{event_id}")
        if event_id == 1:
            # logging.warning(f"mpv event id: {event_id}")
            return True
        return False

    def wait_events(self):
        while True:
            if self.handle_event(mpvmainloop.wait_event(0)):
                break
            time.sleep(MPV_EVENT_WAIT_TIMEOUT)

    def run_in_separate_thread(self):
        self.thread = True
        threading.Thread(target=self.run).start()

    def run(self):
        # mpv.create_stats()
        self.wait_events()
        self.shutdown()

    def shutdown(self):
        if self.thread:
            main_thread = threading.main_thread()
            main_thread.join()
        mpv.shutdown()
logger.warning("running main loop")
clss = globals()['clients']
ml = MainLoop(clss)
ml.run()
