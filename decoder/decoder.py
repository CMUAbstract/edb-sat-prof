import sys
from pycrc.crc_algorithms import Crc

BEACON = 0xED

# see rad_pkt_t in edb-sat/src/payload.h
PKT_CHKSUM_MASK = 0xE0
PKT_TYPE_MASK   = 0x10
PKT_IDX_MASK    = 0x0F

PKT_TYPE_ENERGY_PROFILE = 0
PKT_TYPE_APP_OUTPUT     = 1
PKT_TYPE_BEACON         = 3 # introduce fake type, for legibility

MB_HDR_FIELD_WIDTH_CHKSUM = 4
MB_HDR_FIELD_WIDTH_SIZE = 4
MB_HDR_CHKSUM_MASK = 0xF

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

STATE_NONE = 0
STATE_HDR  = 1

PAYLOAD_STATE_NONE = 0
PAYLOAD_STATE_HDR = 1
PAYLOAD_STATE_HDR_RETRY = 2
PAYLOAD_STATE_DATA = 3
PAYLOAD_STATE_DATA_RETRY = 4

def print_err(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


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

def parse_multibyte_header(h):
    fd = FieldDecoder([h])
    size = fd.decode_field(MB_HDR_FIELD_WIDTH_SIZE)
    chksum = fd.decode_field(MB_HDR_FIELD_WIDTH_CHKSUM)
    return chksum, size


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


class Decoder:

    def __init__(self):

        self.state = STATE_NONE
        self.payload_state = PAYLOAD_STATE_NONE

        self.payload = []


    def decode(self, b):

        #print("BYTE: %02x" % b)

        if b == BEACON:
            self.state = STATE_NONE
            return PKT_TYPE_BEACON, [b]

        elif self.state == STATE_NONE:

            self.hdr_raw = b

            # attempt to interpret as header
            self.pkt_chksum = extract_field(b, PKT_CHKSUM_MASK)
            self.pkt_type = extract_field(b, PKT_TYPE_MASK)
            self.pkt_idx = extract_field(b, PKT_IDX_MASK)

            if self.pkt_type not in [PKT_TYPE_ENERGY_PROFILE, PKT_TYPE_APP_OUTPUT]:
                print_err("invalid pkt type: ", self.pkt_type)
                return

            self.state = STATE_HDR

        elif self.state == STATE_HDR:
            data_byte = b

            actual_chksum = crc([self.hdr_raw & ~PKT_CHKSUM_MASK, data_byte]) & (PKT_CHKSUM_MASK >> shift(PKT_CHKSUM_MASK))

            if actual_chksum != self.pkt_chksum:
                print_err("payload chunk chksum mismatch: %02x (expected %02x)" % (actual_chksum, self.pkt_chksum))
                self.state = STATE_NONE
                return b # put back

            if self.payload_state == PAYLOAD_STATE_NONE:

                self.payload_chksum, self.payload_size = parse_multibyte_header(data_byte)

                # This check is optional
                if self.payload_size != PKT_SIZE_BY_TYPE[self.pkt_type]:
                    print_err("payload size mismatch:", self.payload_size, "(expected", PKT_SIZE_BY_TYPE[self.pkt_type], ")")

                    if self.payload_state == PAYLOAD_STATE_HDR:
                        self.payload_state = PAYLOAD_STATE_HDR_RETRY
                    elif self.payload_state == PAYLOAD_STATE_HDR_RETRY:
                        self.payload_state = PAYLOAD_STATE_NONE

                    return

                self.payload_type = self.pkt_type
                self.payload = []
                self.payload_state = PAYLOAD_STATE_DATA

            elif self.payload_state in [PAYLOAD_STATE_DATA, PAYLOAD_STATE_DATA_RETRY]:

                if self.payload_type != self.pkt_type:
                    print_err("payload chunk type mismatch:", self.pkt_type, "(expected", self.payload_type, ")")

                    #self.payload_state = PAYLOAD_STATE_NONE # TODO
                    if self.payload_state == PAYLOAD_STATE_DATA:
                        self.payload_state = PAYLOAD_STATE_DATA_RETRY
                    elif self.payload_state == PAYLOAD_STATE_DATA_RETRY:
                        self.payload_state = PAYLOAD_STATE_NONE

                    self.state = STATE_NONE
                    return b # put back

                if self.pkt_idx != len(self.payload) + 1: # +1 for header of multibyte pkt
                    print_err("payload chunk idx mismatch:", self.pkt_idx, "(expected", len(self.payload), ")")
                    #self.payload_state = PAYLOAD_STATE_NONE # TODO

                    if self.payload_state == PAYLOAD_STATE_DATA:
                        self.payload_state = PAYLOAD_STATE_DATA_RETRY
                    elif self.payload_state == PAYLOAD_STATE_DATA_RETRY:
                        self.payload_state = PAYLOAD_STATE_NONE

                    self.state = STATE_NONE
                    return b # put back

                self.payload.append(data_byte)

                if len(self.payload) == self.payload_size:

                    actual_chksum = crc(self.payload) & MB_HDR_CHKSUM_MASK
                    if self.payload_chksum != actual_chksum:
                        print_err("payload chksum mismatch:", actual_chksum, "(expected", self.payload_chksum, ")")
                        self.payload_state = PAYLOAD_STATE_NONE
                        return

                    print("pkt decoded: type", self.payload_type, "payload", self.payload);
                    self.payload_state = PAYLOAD_STATE_NONE
                    return self.payload_type, self.payload
                elif len(self.payload) > self.payload_size: # TODO: shouldn't happen
                    self.payload_state = PAYLOAD_STATE_NONE

            self.state = STATE_NONE

        return None
