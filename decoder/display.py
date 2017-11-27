#!/usr/bin/env python

from odroidshow.context import Screen, ScreenContext

import atexit
import sys
import argparse
import os
import fcntl
import time
import select

parser = argparse.ArgumentParser()
parser.add_argument("data_file",
                    help="data file to monitor and display",)
parser.add_argument("--port", "-p",
                    help="serial port to use as the output (default=/dev/ttyUSB0)",
                    type=str, default="/dev/ttyUSB0")
args = parser.parse_args()

ctx = ScreenContext(args.port)

atexit.register(ctx.cleanup)

# Wait 6 seconds for the screen to boot up before we start uploading anything
ctx.sleep(6).reset_lcd().set_rotation(Screen.HORIZONTAL)

fin = open(args.data_file, "rb")
fcntl.fcntl(fin.fileno(), fcntl.F_SETFL, os.O_NONBLOCK)


# Header
ctx.home().bg_color(Screen.BLUE).fg_color(Screen.WHITE).write_line('Packets from Sprite')
ctx.bg_color(Screen.BLACK)

while True:

    readable, writable, exceptional = select.select([fin], [], [fin])
    if fin in exceptional:
        raise Exception("select return exceptional condition")
    if fin in readable:
        d = fin.read()
        while len(d) > 0:
            for b in bytearray(d):
                s = "%02x" % b
                ctx.write(s)
            d = fin.read()
