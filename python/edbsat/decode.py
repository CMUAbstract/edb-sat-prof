#!/usr/bin/python

import argparse
import sys
import select

from edbsat.decoder import *
from edbsat.display import *

parser = argparse.ArgumentParser(
    description="Parse packet data from EDBsat recorded by SDR dongle")
parser.add_argument('data_in', nargs='?',
    help="Input file with binary data")
parser.add_argument('--hex', action='store_true',
    help="Input file is an ASCII text file with hex numbers separated by whitespace")
parser.add_argument("--display", "-d",
                    help="serial port of ODROIDshow LCD screen (e.g., /dev/ttyUSB0)")
parser.add_argument('--output', '-o',
    help="Output file with parsed packets (text)")
parser.add_argument('--output-bytes',
    help="Output file where to save received bytes (binary)")
args = parser.parse_args()

PKT_TYPE_TO_STRING = {
    PKT_TYPE_BEACON: "B",
    PKT_TYPE_ENERGY_PROFILE: "P",
    PKT_TYPE_APP_OUTPUT: "A",
}

n = None
s = None
def parse_hex(ch):
    global n, s

    try:

        if ch is None:
            if s is not None:
                h = int(s, 16)
                s = None
                return h
            else:
                return None

        bs = ch
        if len(bs) == 0:
            if s is not None:
                h = int(s, 16)
                s = None
                return h
            else:
                return None

        b = bs[0]
        if b in [' ', '\t', '\n']:
            if s is not None:
                print("s=", s)
                h = int(s, 16)
                s = None
                return h
            else:
                return None
        else:
            if s is None:
                s = None
            if s is None:
                s = ""
            s += b
    except:
        s = None
        return None

if args.data_in:
    if args.hex:
        fin_mode = ""
    else:
        fin_mode = "b"
    fin = open(args.data_in, "r" + fin_mode)
else:
    fin = sys.stdin

if args.output:
    fout = open(args.output, "w")
else:
    fout = sys.stdout

if args.output_bytes:
    output_bytes = open(args.output_bytes, "wb")

def format_pkt(payload_type, payload):
    s = ""

    if payload_type == PKT_TYPE_BEACON:
        s = "B"

    elif payload_type == PKT_TYPE_ENERGY_PROFILE:
        s = "P: "
        field_dec = FieldDecoder(payload)
        
        p = 0
        for i in range(PROFILE_NUM_EVENTS):
            bins = []
            for j in range(PROFILE_BINS):
                c = field_dec.decode_field(PROFILE_FIELD_WIDTH_BIN)
                bins.append(c)
            count = field_dec.decode_field(PROFILE_FIELD_WIDTH_COUNT)

            s += "| %u [%s] " % (count, ":".join(map(str, bins)))

    elif payload_type == PKT_TYPE_APP_OUTPUT:
        s = "A: "
        field_dec = FieldDecoder(payload)

        for i in range(APPOUT_NUM_WINDOWS):
            temp = twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_TEMP), APPOUT_FIELD_WIDTH_TEMP)

            m = []
            for i in range(APPOUT_NUM_AXES_MAG):
                m.append(twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_MAG), APPOUT_FIELD_WIDTH_MAG))
            a = []
            for i in range(APPOUT_NUM_AXES_ACCEL):
                a.append(twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_ACCEL), APPOUT_FIELD_WIDTH_ACCEL))

            s += "[temp %u mag (%s) accel (%s)] " % \
                    (temp, ",".join(map(str, m)), ",".join(map(str,a)))
    return s


inbuf = []

if args.display:
    display = Display(port=args.display)

decoder = Decoder()

while True:

    if len(inbuf) == 0:

        readable, writeable, exceptional = select.select([fin], [], [fin])
        if fin in exceptional:
            raise Exception("select encountered error")

        if fin in readable:
            bs = fin.read(1)
            if len(bs) == 0:
                no_data = True
                if args.hex:
                    new_b = parse_hex(None) # flush current token
                    if new_b is None:
                        no_data = True
                else: 
                    no_data = True

                if no_data:
                    time.sleep(1) # on fifo pipes, select returns (?)
                    continue # eof

        if args.hex:
            new_b = parse_hex(bs)
            if new_b is None:
                continue # no token, keep going
        else:
            new_b = bs[0]

        if output_bytes is not None:
            output_bytes.write(bytes([new_b]))
            output_bytes.flush()

        inbuf = [int(new_b)] + inbuf

    b = inbuf.pop()

    pkt = decoder.decode(b)

    put_back = False
    if pkt is not None:
        if pkt == b: # put back
            inbuf.append(b)
            put_back = True
        else:
            payload_type, payload = pkt

            pkt_s = "%s" % PKT_TYPE_TO_STRING[payload_type]
            if len(payload) > 0:
                pkt_s += ": " + " ".join(["%02x" % b for b in payload])
            print(pkt_s)

            pkt_str = format_pkt(payload_type, payload)
            fout.write(pkt_str + "\n")
            display.show_pkt(pkt_str)

    if args.display and not put_back:
        display.show_bytes([b])
