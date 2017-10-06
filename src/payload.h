#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <libedb/target_comm.h>

#ifdef CONFIG_COLLECT_ENERGY_PROFILE
#include "profile.h"
#endif

#include "flash.h"

typedef enum {
    // BEACON: does not a type: determined from content
    // also, beacons are not saved in flash before transmission
    PKT_TYPE_ENERGY_PROFILE     = 0,
    PKT_TYPE_APP_OUTPUT         = 1,
    // NOTE: field size is 1 bit
} pkt_type_t;

#define BEACON 0xED

// Header of packet saved in flash
// NOTE: could shrink to 1 byte by having fixed size and a 3-bit header CRC
typedef struct __attribute__((packed)) {
        // little-endian means these bits in mem are in opposite order
        unsigned hdr_chksum:4;
        unsigned pay_chksum:4;
        unsigned size:4; // bytes
        unsigned padded:1; // whether the pkt payload size is odd
        pkt_type_t type:1;
} pkt_header_t;

#define PKT_HDR_DATA(h) (h & 0xfff0) // mask hdr_checksum

typedef union __attribute__((packed)) {
    pkt_header_t typed;
    uint16_t raw;
} pkt_header_union_t;

// Packet descriptor to hold info about a packet in NV memory
typedef struct __attribute__((packed)) {
    uint16_t sent_mask; // could shrink to 1 word if we can send 2 payload bytes at a time
    pkt_header_union_t header;
} pkt_desc_t;

typedef union __attribute__((packed)) {
    pkt_desc_t typed;
    uint32_t raw;
} pkt_desc_union_t;
#define PAYLOAD_DESC_SIZE 4 // bytes

#define AS_PKT_DESC(addr) (*((uint32_t *)addr))

// Fixed-size packet sent over the radio
typedef struct __attribute__((packed)) {
    // little-endian means these bits in mem are in opposite order
    unsigned idx:4;
    unsigned type:1;
    unsigned chksum:3;
    uint8_t payload_byte;
} rad_pkt_t;

#define RAD_PKT_CHKSUM_MASK 0x0007

typedef union __attribute__((packed)) {
    rad_pkt_t typed;
    uint16_t raw;
} rad_pkt_union_t;

void payload_init();
void payload_send_beacon();
bool payload_send_pkt(rad_pkt_union_t *pkt);

flash_status_t save_payload(flash_loc_t *loc, pkt_type_t pkt_type, uint8_t *pkt_data, unsigned len);
bool transmit_saved_payload();

#endif // PAYLOAD_H
