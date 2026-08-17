#!/usr/bin/env python3
"""Capture serial console output right after a flash, without a full
interactive `idf.py monitor` session. Resets the board via DTR/RTS (same
sequence esptool/idf_monitor use) and prints whatever it logs for a fixed
window.

Usage: python3 tools/serial_monitor.py [port] [seconds]
Defaults: /dev/ttyACM0, 8 seconds.
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

ser = serial.Serial(port, 115200, timeout=1)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
time.sleep(0.1)

end = time.time() + duration
buf = b""
while time.time() < end:
    data = ser.read(4096)
    if data:
        buf += data

sys.stdout.write(buf.decode(errors="replace"))
