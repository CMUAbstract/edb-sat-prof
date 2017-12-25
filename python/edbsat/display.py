from odroidshow.context import Screen, ScreenContext

import atexit
import sys
import argparse
import os
import fcntl
import time

class Display:

    def __init__(self, port):

        self.ctx = ScreenContext(port)

        atexit.register(self.ctx.cleanup)

        # Wait 6 seconds for the screen to boot up before we start uploading anything
        self.ctx.sleep(6).reset_lcd().set_rotation(Screen.HORIZONTAL)

        self.bytes_rows = self.ctx.get_rows() // 2
        self.pkts_rows = self.ctx.get_rows() - self.bytes_rows


        self.bytes_header = 1
        self.pkts_header = 1
        self.cursor_bytes_offset = self.bytes_header
        self.cursor_pkts_offset = self.bytes_rows + self.pkts_header

        self.cursor_bytes_row = 0
        self.cursor_bytes_col = 0
        self.cursor_pkts_row = 0
        self.cursor_pkts_col = 0

        self.ctx.home()

        print("bytes rows=", self.bytes_rows, "off=", self.cursor_bytes_offset)
        print("pkts rows=", self.pkts_rows, "off=", self.cursor_pkts_offset)

        # Header
        self.ctx.bg_color(Screen.BLUE).fg_color(Screen.WHITE).write_line('Bytes')
        self.ctx.bg_color(Screen.BLACK)

        #time.sleep(5)

        print("p loc ", self.bytes_rows, 0)
        self.ctx.set_cursor_loc(self.bytes_rows, 0)
        self.ctx.bg_color(Screen.BLUE).fg_color(Screen.WHITE).write_line('Packets')
        self.ctx.bg_color(Screen.BLACK)

        #time.sleep(5)


    def show_bytes(self, d):

        for b in bytearray(d):
            s = "%02x " % b

            if self.cursor_bytes_col + len(s) > self.ctx.get_columns():
                self.cursor_bytes_col = 0
                self.cursor_bytes_row = (self.cursor_bytes_row + 1) % (self.bytes_rows - self.bytes_header) # 1+ and -1 for header
                if self.cursor_bytes_row == 0:
                    row = 0
                    for i in range(self.bytes_rows - self.bytes_header):
                        self.ctx.set_cursor_loc(self.cursor_bytes_offset + row, 0)
                        self.ctx.write(' ' * self.ctx.get_columns())
                        row += 1

            self.ctx.set_cursor_loc(self.cursor_bytes_offset + self.cursor_bytes_row, self.cursor_bytes_col)
            print("b loc ", self.cursor_bytes_offset + self.cursor_bytes_row, self.cursor_bytes_col)

            self.ctx.write(s)
            self.cursor_bytes_col += len(s)

    def show_pkt(self, pkt):
        # pkt is just one or more lines of text
        if self.cursor_pkts_col + len(pkt) + 1 > self.ctx.get_columns(): # +1 for leading space
            pkt_lines = len(pkt) // self.ctx.get_columns() + 1

            if self.cursor_pkts_row + pkt_lines > (self.pkts_rows - self.pkts_header):
                self.cursor_pkts_row = 0
                self.ctx.set_cursor_loc(self.cursor_pkts_offset, 0)
                for i in range(self.pkts_rows - self.pkts_header):
                    self.ctx.write_line(' ' * self.ctx.get_columns())

            self.ctx.set_cursor_loc(self.cursor_pkts_offset + self.cursor_pkts_row, 0)
            print("p loc ", self.cursor_pkts_offset + self.cursor_pkts_row, 0)
            self.ctx.write_line(pkt)
            print("line ", pkt)

            #self.cursor_pkts_col = len(pkt) % self.ctx.get_columns()
            self.cursor_pkts_col = 0
            self.cursor_pkts_row += pkt_lines
        else:
            pkt = " " + pkt
            self.ctx.set_cursor_loc(self.cursor_pkts_offset + self.cursor_pkts_row, self.cursor_pkts_col)
            self.ctx.write(pkt)
            self.cursor_pkts_col += len(pkt)
