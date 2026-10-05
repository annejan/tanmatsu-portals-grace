#!/usr/bin/env python3
"""Get the badge back to the launcher after a crash or hang.

Resets the P4 over its debug console with RTS, as esptool does for a
USB-Serial/JTAG chip. If the app comes up again and announces itself
(READY/PONG), it is sent EXIT so it returns to the launcher. If nothing
answers within the timeout, the badge is assumed to be in the launcher.

From tanmatsu-idf6tests' tools/recover.py; plain pyserial (badgelink's
virtualenv has it). The port must be the one tools/p4port.sh finds: on a
Tanmatsu the other Espressif tty is the ESP32-C6's, and resetting that
crashes the badge.

Exit codes: 0 app answered and was sent EXIT, 4 reset done but no app
answered (probably in the launcher), 1 could not reset.
"""

import argparse
import os
import sys
import time


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testrun import connect, open_console, parse_record, read_line  # noqa: E402


def hard_reset(url):
    """RTS on while DTR is off holds a USB-Serial/JTAG chip in reset; RTS off
    lets it boot, normally, as DTR is off (esptool's HardReset for USB)."""
    port = open_console(url)  # checks the port; leaves RTS, then DTR, off
    port.rts = True
    time.sleep(0.2)
    port.rts = False
    time.sleep(0.2)
    port.close()


def recover(url, timeout=60, log=None):
    log = log or open(os.devnull, "w")
    print(f"Recovery: resetting the badge via {url}...")
    try:
        hard_reset(url)
    except Exception as exc:  # noqa: BLE001
        print(f"Recovery: reset failed: {exc}", file=sys.stderr)
        return 1
    port, ready = connect(url, timeout, log)
    if port is None:
        print("Recovery: no app answered after the reset (probably in the launcher)")
        return 4
    print(f"Recovery: {ready.get('app')} answered, sending EXIT")
    buffer = bytearray()
    try:
        port.write(b"EXIT\n")
        port.flush()
        deadline = time.time() + 5
        while time.time() < deadline:
            line = read_line(port, buffer)
            if line is not None and parse_record(line)[0] == "BYE":
                break
    except Exception:  # noqa: BLE001 - the link drops as the badge restarts
        pass
    try:
        port.close()
    except Exception:  # noqa: BLE001
        pass
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="the P4's debug console, from tools/p4port.sh; $PORT is not read")
    ap.add_argument("--timeout", type=float, default=60)
    args = ap.parse_args()
    return recover(args.port, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
