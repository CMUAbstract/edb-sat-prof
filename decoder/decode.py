#!/usr/bin/python

import argparse
import sys
from pycrc.crc_algorithms import Crc

parser = argparse.ArgumentParser(
    description="Parse packet data from EDBsat recorded by SDR dongle")
parser.add_argument('data_in', nargs='?',
    help="Input file with binary data")
parser.add_argument('--hex', action='store_true',
    help="Input file is an ASCII text file with hex numbers separated by whitespace")
parser.add_argument('--output', '-o',
    help="Output file with parsed packets (text)")
args = parser.parse_args()

def print_err(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

BEACON = 0xED

# see rad_pkt_t in edb-sat/src/payload.h
PKT_CHKSUM_MASK = 0xE0
PKT_TYPE_MASK   = 0x10
PKT_IDX_MASK    = 0x0F

PKT_TYPE_ENERGY_PROFILE = 0
PKT_TYPE_APP_OUTPUT     = 1
PKT_TYPE_BEACON         = 3 # introduce fake type, for legibility

PKT_TYPE_TO_STRING = {
    PKT_TYPE_BEACON: "BEACON",
    PKT_TYPE_ENERGY_PROFILE: "PROFILE",
    PKT_TYPE_APP_OUTPUT: "APPOUT",
}

PROFILE_NUM_EVENTS = 4 # see edb-sat/src/profile.h
PROFILE_BINS = 2
PROFILE_BITS_PER_BIN = 4

PROFILE_FIELD_WIDTH_BIN = 5
PROFILE_FIELD_WIDTH_COUNT = 6

APPOUT_NUM_WINDOWS = 2
APPOUT_NUM_AXES_MAG = 3
APPOUT_NUM_AXES_ACCEL = 3

APPOUT_FIELD_WIDTH_TEMP = 8
APPOUT_FIELD_WIDTH_MAG = 4
APPOUT_FIELD_WIDTH_ACCEL = 4

PKT_SIZE_BY_TYPE = {
    PKT_TYPE_ENERGY_PROFILE: PROFILE_NUM_EVENTS * 2,
    PKT_TYPE_APP_OUTPUT:     8,
}

crc_alg = Crc(width=16,
              poly=0x1021,
              xor_in=0xFFFF,
              xor_out=0x0000,
              reflect_in=True,
              reflect_out=False)

class FieldDecoder:
    def __init__(self, data_bytes):
        self.data_bytes = data_bytes[::-1] # reverse for poping
        self.byte = None
        self.bits = 0

    def decode_field(self, width):
        field = 0

        for i in range(8):
            if i >= width:
                b = 0
            else:
                if self.bits == 0:
                    if len(self.data_bytes) == 0:
                        raise Exception("Failed to decode field: insufficient bits in input stream")
                    self.byte = self.data_bytes.pop()
                    self.bits = 8

                b = self.byte & 0x1
                self.byte >>= 1
                self.bits -= 1

            field >>= 1
            field |= b << 7

        return field

def read_hex(f):
    n = None
    s = None

    while True:
        if not f:
            if s is not None:
                return int(s, 16)
            else:
                return None

        bs = f.read(1)
        if len(bs) != 1:
            if s is not None:
                return int(s, 16)
            else:
                return None

        b = bs[0]
        if b in [' ', '\t', '\n']:
            if s is not None:
                return int(s, 16)
            else:
                continue
        else:
            if s is None:
                s = ""
            s += b

def shift(mask):
    n = 0
    while mask & 0x1 == 0:
        n += 1
        mask >>= 1
    return n

def extract_field(v, mask):
    return (v & mask) >> shift(mask)

def twocomp(n, nbits):
    if n & (0x1 << (nbits - 1)):
        return -(2**nbits - n) # two's complement
    else:
        return n

def crc(b):
    #print("CHK: %02x" % crc_alg.bit_by_bit_fast(b"123456789"))
    return crc_alg.bit_by_bit_fast(bytearray(b))

def format_pkt(f, payload_type, payload):
    f.write("%s: %s\n" % \
            (PKT_TYPE_TO_STRING[payload_type], " ".join(["%02x" % b for b in payload])))

    if payload_type == PKT_TYPE_ENERGY_PROFILE:

        field_dec = FieldDecoder(payload)
        
        f.write(">>> PROFILE: ")

        p = 0
        for i in range(PROFILE_NUM_EVENTS):
            bins = []
            for j in range(PROFILE_BINS):
                c = field_dec.decode_field(PROFILE_FIELD_WIDTH_BIN)
                bins.append(c)
            count = field_dec.decode_field(PROFILE_FIELD_WIDTH_COUNT)

            f.write("| %u [%s] " % (count, ":".join(map(str, bins))))
        f.write("\n")

    elif payload_type == PKT_TYPE_APP_OUTPUT:
        field_dec = FieldDecoder(payload)

        f.write(">>> APPOUT: ")

        for i in range(APPOUT_NUM_WINDOWS):
            temp = twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_TEMP), APPOUT_FIELD_WIDTH_TEMP)

            m = []
            for i in range(APPOUT_NUM_AXES_MAG):
                m.append(twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_MAG), APPOUT_FIELD_WIDTH_MAG))
            a = []
            for i in range(APPOUT_NUM_AXES_ACCEL):
                a.append(twocomp(field_dec.decode_field(APPOUT_FIELD_WIDTH_ACCEL), APPOUT_FIELD_WIDTH_ACCEL))

            f.write("[temp %u mag (%s) accel (%s)] " % \
                    (temp, ",".join(map(str, m)), ",".join(map(str,a))))
        f.write("\n")

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

STATE_NONE = 0
STATE_HDR  = 1

PAYLOAD_STATE_NONE = 0
PAYLOAD_STATE_DATA = 1
PAYLOAD_STATE_DATA_RETRY = 2

state = STATE_NONE
payload_state = PAYLOAD_STATE_NONE

payload = []
inbuf = []

while fin:

    if len(inbuf) == 0:
        if args.hex:
            new_b = read_hex(fin)
            if new_b is None:
                break # eof
        else:
            bs = fin.read(1)
            if len(bs) == 0:
                break # eof

            new_b = bs[0]
        inbuf = [new_b] + inbuf

    b = inbuf.pop()

    #print("BYTE: %02x" % b)

    if b == BEACON:
        format_pkt(fout, PKT_TYPE_BEACON, [b])
        state = STATE_NONE

    elif state == STATE_NONE:

        hdr_raw = b

        # attempt to interpret as header
        pkt_chksum = extract_field(b, PKT_CHKSUM_MASK)
        pkt_type = extract_field(b, PKT_TYPE_MASK)
        pkt_idx = extract_field(b, PKT_IDX_MASK)

        if pkt_type not in [PKT_TYPE_ENERGY_PROFILE, PKT_TYPE_APP_OUTPUT]:
            print_err("invalid pkt type: ", pkt_type)
            continue

        payload_size = PKT_SIZE_BY_TYPE[pkt_type]

        state = STATE_HDR

    elif state == STATE_HDR:
        data_byte = b

        actual_chksum = crc([hdr_raw & ~PKT_CHKSUM_MASK, data_byte]) & (PKT_CHKSUM_MASK >> shift(PKT_CHKSUM_MASK))

        if actual_chksum != pkt_chksum:
            print_err("payload chunk chksum mismatch: %02x (expected %02x)" % (actual_chksum, pkt_chksum))
            state = STATE_NONE
            inbuf.append(b) # put back
            continue

        if payload_state == PAYLOAD_STATE_NONE:

            payload_type = pkt_type
            payload = [data_byte]

            payload_state = PAYLOAD_STATE_DATA

        elif payload_state in [PAYLOAD_STATE_DATA, PAYLOAD_STATE_DATA_RETRY]:

            if payload_type != pkt_type:
                print_err("payload chunk type mismatch:", pkt_type, "(expected", payload_type, ")")

                #payload_state = PAYLOAD_STATE_NONE # TODO
                if payload_state == PAYLOAD_STATE_DATA:
                    payload_state = PAYLOAD_STATE_DATA_RETRY
                elif payload_state == PAYLOAD_STATE_DATA_RETRY:
                    payload_state = PAYLOAD_STATE_NONE

                state = STATE_NONE
                inbuf.append(b) # put back
                continue

            if pkt_idx != len(payload):
                print_err("payload chunk idx mismatch:", pkt_idx, "(expected", len(payload), ")")
                #payload_state = PAYLOAD_STATE_NONE # TODO

                if payload_state == PAYLOAD_STATE_DATA:
                    payload_state = PAYLOAD_STATE_DATA_RETRY
                elif payload_state == PAYLOAD_STATE_DATA_RETRY:
                    payload_state = PAYLOAD_STATE_NONE

                state = STATE_NONE
                inbuf.append(b) # put back
                continue

            payload.append(data_byte)

            if len(payload) == payload_size:
                print("pkt decoded: type", payload_type, "payload", payload);
                format_pkt(fout, payload_type, payload)
                payload_state = PAYLOAD_STATE_NONE
            elif len(payload) > payload_size:
                payload_state = PAYLOAD_STATE_NONE

        state = STATE_NONE
