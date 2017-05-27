#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <libedb/target_comm.h>

#ifdef CONFIG_COLLECT_ENERGY_PROFILE
#include "profile.h"
#endif

typedef enum {
    PKT_TYPE_BEACON             = 1,
    PKT_TYPE_APP_OUTPUT         = 2,
    PKT_TYPE_ENERGY_PROFILE     = 3,
} pkt_type_t;

typedef enum {
    PKT_FLAG_NOT_SENT           = 1,
} pkt_flags_t;

typedef struct {
        pkt_type_t type:2;
        pkt_flags_t flags:2;
        uint8_t size:4; // bytes
} pkt_header_t;

typedef union {
    pkt_header_t typed;
    uint8_t raw;
} pkt_header_union_t;

// Packet descriptor to hold info about a packet in NV memory
typedef struct {
        pkt_header_union_t header;
        uint8_t chksum_header:4;
        uint8_t chksum_payload:4;
} pkt_desc_t;

typedef union {
    pkt_desc_t typed;
    uint16_t raw;
} pkt_desc_union_t;
#define PAYLOAD_DESC_SIZE 2 // bytes

/*
 * @brief Aggregate data packet sent from sprite to host/ground
 */
typedef struct {
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
    profile_t energy_profile;
#endif
#ifdef CONFIG_COLLECT_APP_OUTPUT
    uint8_t app_output[APP_OUTPUT_SIZE];
#endif
} payload_t;

void payload_init();
void payload_send_beacon();
void payload_send_profile();
void payload_send_app_output();
void payload_send(); // do no use, it's too big

#ifdef CONFIG_COLLECT_ENERGY_PROFILE
bool payload_record_profile_event(unsigned index, uint16_t vcap);
#endif
#ifdef CONFIG_COLLECT_APP_OUTPUT
void payload_record_app_output(const uint8_t *data, unsigned len);
#endif

void send_payload(payload_t *payload);

#endif // PAYLOAD_H
