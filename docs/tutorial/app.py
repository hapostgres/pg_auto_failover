#! /usr/bin/env python3

#
# Write a Python application that knows how to exit gracefully when
# receiving SIGTERM (at docker compose down time), but doesn't know how to
# do much else.
#

import sys
import time
import signal
from datetime import datetime


def sigterm_handler(_signo, _stack_frame):
    sys.exit(0)


signal.signal(signal.SIGTERM, sigterm_handler)

if __name__ == "__main__":
    try:
        while True:
            print("%s" % datetime.now())
            time.sleep(600)
    except BaseException:
        # Keyboard Interrupt or something
        pass
