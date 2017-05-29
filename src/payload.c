#include <msp430.h>
#include <string.h>
#include <stdlib.h>

#include <libmsp/periph.h>

#ifdef CONFIG_DEV_CONSOLE
#include <libio/log.h>
#endif

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
#include <libsprite/SpriteRadio.h>
#endif

#include <libedbserver/uart.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/error.h>
#include <libedbserver/pin_assign.h>
#include <libedbserver/host_comm_impl.h>

#include "payload.h"
#include "flash.h"
#include "bits.h"

// TODO: HACK
// From app: must match!
#define NUM_SENSORS 7
#define NUM_WINDOWS 4
typedef struct _edb_info_t{
  int8_t averages[NUM_SENSORS][NUM_WINDOWS];
} edb_info_t;

#if 0
static payload_t payload; // EDB+App data sent to host/ground
#else
payload_t payload; // EDB+App data sent to host/ground
#endif

static uint8_t host_msg_buf[HOST_MSG_BUF_SIZE];
static uint8_t * const host_msg_payload = &host_msg_buf[UART_MSG_HEADER_SIZE];

static void log_packet(char type, uint8_t header, uint8_t *pkt, unsigned len)
{
#if defined(CONFIG_DEV_CONSOLE)
    int i;
    BLOCK_LOG_BEGIN();
    BLOCK_LOG("tx: pkt %c:\r\n", type);
    BLOCK_LOG("%02x ", header);
    for (i = 0; i < len; ++i) {
        BLOCK_LOG("%02x ", *((uint8_t *)pkt + i));

        if (((i + 1 + 1) & (8 - 1)) == 0)
            BLOCK_LOG("\r\n");
    }
    BLOCK_LOG("\r\n");
    BLOCK_LOG_END();
#endif
}

void payload_init()
{
#ifdef CONFIG_COLLECT_APP_OUTPUT
    memset(&payload.app_output, 0, sizeof(payload.app_output));
#endif

// TODO: move to main, because this is too major
#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio();
#endif
}

void payload_send_beacon()
{
    uint8_t pkt = 0xED;

    log_packet('B', 0, &pkt, sizeof(pkt));

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio(); // only one tx per boot, so init here
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)&pkt, sizeof(pkt));
    SpriteRadio_sleep();
#endif
}

#ifdef CONFIG_COLLECT_APP_OUTPUT
void payload_send_app_output()
{
    // randomply pick one sensor and send only that

    uint8_t sensor_idx = rand() % NUM_SENSORS;

    uint8_t *pkt = (uint8_t *)(&payload.app_output) + sensor_idx * NUM_WINDOWS;
    unsigned pkt_len = NUM_WINDOWS * sizeof(int8_t);

    uint8_t header = (PKT_TYPE_APP_OUTPUT << 4) | (sensor_idx & 0x0f);

    log_packet('A', header, pkt, pkt_len);

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio();
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)&header, sizeof(header));
    SpriteRadio_transmit((char *)pkt, pkt_len);
    SpriteRadio_sleep();
#endif // CONFIG_RADIO_TRANSMIT_PAYLOAD
}
#endif // CONFIG_COLLECT_APP_OUTPUT

#ifdef CONFIG_COLLECT_ENERGY_PROFILE
void payload_send_profile()
{
    // randomply pick one watchpoint and send only that
    int wp_idx = rand() % (NUM_EVENTS - 1); // 3rd watchpoint unused

    uint8_t *pkt = (uint8_t *)(&payload.energy_profile.events[0] + wp_idx);
    unsigned pkt_len = NUM_ENERGY_BYTES + 1; // 1 is for count; this is sizeof(event_t) without padding

    uint8_t header = (PKT_TYPE_ENERGY_PROFILE << 4) | (wp_idx & 0x0f);

    log_packet('E', header, pkt, pkt_len);

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)&wp_idx, pkt_len);
    SpriteRadio_transmit((char *)&pkt, pkt_len);
    SpriteRadio_sleep();
#endif // CONFIG_RADIO_TRANSMIT_PAYLOAD
}
#endif // COLLECT_ENERGY_PROFILE

void payload_send()
{
    log_packet('P', 0, (uint8_t *)&payload, sizeof(payload_t));

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)&payload, sizeof(payload_t));
    SpriteRadio_sleep();
#endif // CONFIG_RADIO_TRANSMIT_PAYLOAD
}

#ifdef CONFIG_COLLECT_APP_OUTPUT
void payload_record_app_output(const uint8_t *data, unsigned len)
{
    ASSERT(ASSERT_APP_OUTPUT_BUF_OVERFLOW, len <= sizeof(payload.app_output));
    memcpy(&payload.app_output, data, len);
}
#endif // CONFIG_COLLECT_APP_OUTPUT

#ifdef CONFIG_SEND_PAYLOAD_TO_HOST
void send_payload(payload_t *payload)
{
    // The '*payload*' variables here refer to the payload of the message that
    // is being sent to host.  The argument 'payload' is just happens to be
    // also called payload.

    unsigned payload_len = 0;
    UART_begin_transmission();

    memcpy(host_msg_payload, payload, sizeof(payload_t));
    payload_len += sizeof(payload_t);

    send_msg_to_host(USB_RSP_ENERGY_PROFILE, payload_len);
}
#endif // CONFIG_SEND_PAYLOAD_TO_HOST

bool send_pkt(rad_pkt_union_t *pkt)
{
    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = pkt->raw & RAD_PKT_CHKSUM_MASK; // mask chksum just in case caller didn't zero it
    pkt->typed.chksum = CRCINIRES & RAD_PKT_CHKSUM_MASK;
    LOG("rad pkt chksum: %02x\r\n", pkt->typed.chksum);
    LOG("trasmiting pkt: 0x%04x\r\n", pkt->raw);

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio(); // only one tx per boot, so init here
    SpriteRadio_txInit();
    SpriteRadio_transmit((uint8_t *)pkt, sizeof(rad_pkt_t));
    SpriteRadio_sleep();
#endif // CONFIG_RADIO_TRANSMIT_PAYLOAD

    LOG("trasmit finished\r\n");
    return true;
}

// 'loc' must be the result of a successful call to flash_find_space()
// Len < 2^16
flash_status_t save_payload(flash_loc_t *loc, pkt_type_t pkt_type, uint8_t *pkt_data, unsigned len)
{
    pkt_desc_union_t pkt_desc = { .typed = { .sent_mask = 0xFFFF >> (16 - len),
                                             .header = { .typed = { .type = pkt_type,
                                                                    .size = len,
                                                                    .padded = (loc->bit_idx & 0x1) ^ (len & 0x1),
                                                                    .pay_chksum = 0,
                                                                    .hdr_chksum = 0,
                                                                } } } };

    CRCINIRES = 0xFFFF; // init value for checksum
    for (int i = 0; i < len; ++i)
         CRCDI = *(pkt_data + i);
    pkt_desc.typed.header.typed.pay_chksum = CRCINIRES & 0x0f;
    LOG("payload chksum: %02x\r\n", pkt_desc.typed.header.typed.pay_chksum);

    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = pkt_desc.typed.header.raw;
    pkt_desc.typed.header.typed.hdr_chksum = CRCINIRES & 0x0f;
    LOG("hdr chksum: %02x\r\n", pkt_desc.typed.header.typed.hdr_chksum);

    LOG("saving pkt to flash: \r\n");
    for (int i = 0; i < len; ++i) {
        LOG("%02x ", *(pkt_data + i));
    }
    LOG("(%04x %04x)\r\n", (uint16_t)(pkt_desc.raw >> 16), (uint16_t)(pkt_desc.raw & 0xffff));

    unsigned padded = pkt_desc.typed.header.typed.padded;
    uint8_t *pkt_saved = flash_alloc(loc, len + padded + PAYLOAD_DESC_SIZE);
    if (!pkt_saved) {
        return FLASH_STATUS_ALLOC_FAILED;
    }
    uint8_t *desc_addr = pkt_saved + len + padded;
    // assert !(desc_addr & 0x1) [word aligned]
    if (!flash_write(pkt_saved, (uint8_t *)&profile, len)) {
        return FLASH_STATUS_WRITE_FAILED;
    }
    if (!flash_write(desc_addr, (uint8_t *)&pkt_desc.raw, PAYLOAD_DESC_SIZE)) {
        return FLASH_STATUS_WRITE_FAILED;
    }

    return FLASH_STATUS_OK;
}

bool transmit_saved_payload()
{
    LOG("look for unsent pkt in flash\r\n");
    uint8_t *saved_pkt_desc_addr = flash_find_last_byte();
    LOG("pkt descriptor @0x%04x\r\n", (uint16_t)saved_pkt_desc_addr);
    if (!saved_pkt_desc_addr) {
        LOG("no saved pkt found in flash\r\n");
        return false;
    }
    saved_pkt_desc_addr -= PAYLOAD_DESC_SIZE - 1; // move to first byte of the pkt descriptor

    pkt_desc_union_t saved_pkt_desc_union;
    saved_pkt_desc_union.raw = AS_PKT_DESC(saved_pkt_desc_addr); // read from flash
    pkt_desc_t saved_pkt_desc = saved_pkt_desc_union.typed;
    pkt_header_t saved_pkt_header = saved_pkt_desc.header.typed;

    LOG("pkt descriptor: %04x %04x: type %u size %u pad %u | chksum: payload %x hdr %x\r\n",
        (uint16_t)(saved_pkt_desc_union.raw >> 16), (uint16_t)(saved_pkt_desc_union.raw & 0xFFFF),
        saved_pkt_header.type, saved_pkt_header.size, saved_pkt_header.padded,
        saved_pkt_header.pay_chksum, saved_pkt_header.hdr_chksum);

    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = PKT_HDR_DATA(saved_pkt_desc.header.raw);
    unsigned hdr_chksum = CRCINIRES & 0x0f;
    if (hdr_chksum != saved_pkt_header.hdr_chksum) {
        LOG("pkt header checksum mismatch (%02x != %02x): igoring pkt\r\n",
            hdr_chksum, saved_pkt_header.hdr_chksum);
        return false;
    }
    LOG("pkt header valid\r\n");

    if (!saved_pkt_desc.sent_mask) {
        LOG("pkt already marked sent\r\n");
        return false;
    }

    uint8_t *saved_pkt_addr = ((uint8_t*)saved_pkt_desc_addr) - saved_pkt_header.size - saved_pkt_header.padded;

    LOG("pkt payload (addr 0x%04x len %u): ", (uint16_t)saved_pkt_addr, saved_pkt_header.size);
    for(int i = 0; i < saved_pkt_header.size; ++i) {
        LOG("%02x ", *(saved_pkt_addr + i));
    }
    LOG("\r\n");

    CRCINIRES = 0xFFFF; // init value for checksum
    for(int i = 0; i < saved_pkt_header.size; ++i) {
        CRCDI = *(saved_pkt_addr + i);
    }
    unsigned pay_chksum = CRCINIRES & 0x0f;
    if (pay_chksum != saved_pkt_header.pay_chksum) {
        LOG("payload checksum mismatch (%02x != %02x): ignoring pkt\r\n",
            pay_chksum, saved_pkt_header.pay_chksum);
        return false;
    }

    unsigned sent_mask_offset = 16 - saved_pkt_header.size;
    uint8_t unsent_byte_idx = find_first_set_bit_in_word(saved_pkt_desc.sent_mask) - sent_mask_offset;
    // assert unsent_byte_idx < 16 because we checked for zero above
    LOG("unsent byte idx: %u\r\n", unsent_byte_idx);
    uint8_t payload_byte = *(saved_pkt_addr + unsent_byte_idx);

    rad_pkt_union_t pkt = { .typed = {
        .chksum = 0,
        .type = saved_pkt_header.type,
        .idx = unsent_byte_idx,
        .payload_byte = payload_byte
    } };
    if (!send_pkt(&pkt)) {
        LOG("failed to send pkt\r\n");
        return false;
    }

    saved_pkt_desc.sent_mask &= ~(1 << (15 - sent_mask_offset - unsent_byte_idx));
    LOG("markig pkt at 0x%04x as sent: sent mask 0x%04x\r\n", (uint16_t)saved_pkt_desc_addr, saved_pkt_desc.sent_mask);
    flash_write_word((uint16_t *)saved_pkt_desc_addr, saved_pkt_desc.sent_mask);
    LOG("updated sent mask: @0x%04x [0x%04x]\r\n", (uint16_t)saved_pkt_desc_addr, *(uint16_t *)saved_pkt_desc_addr);

    return true;
}
