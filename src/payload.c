#include <msp430.h>
#include <string.h>
#include <stdlib.h>

#include <libmsp/periph.h>
#include <libio/console.h>

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

void payload_send_beacon()
{
    uint8_t pkt = BEACON;

    LOG("transmitting beacon: 0x%02x\r\n", pkt);

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio(); // only one tx per boot, so init here
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)&pkt, sizeof(pkt));
    SpriteRadio_sleep();
#endif
}

bool payload_send_pkt(rad_pkt_union_t *pkt)
{
    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = (uint16_t)(pkt->raw & RAD_PKT_CHKSUM_MASK); // mask chksum just in case caller didn't zero it
    __delay_cycles(2); // word CRC takes 2 cycles, so need to delay by at least 1
    pkt->typed.chksum = CRCINIRES & RAD_PKT_CHKSUM_MASK;
    LOG("rad pkt chksum: %02x\r\n", pkt->typed.chksum);
    LOG("trasmiting pkt: 0x%04x\r\n", pkt->raw);

#ifdef CONFIG_RADIO_TRANSMIT_PAYLOAD
    SpriteRadio_SpriteRadio(); // only one tx per boot, so init here
    SpriteRadio_txInit();
    SpriteRadio_transmit((char *)pkt, sizeof(rad_pkt_t));
    SpriteRadio_sleep();
#endif // CONFIG_RADIO_TRANSMIT_PAYLOAD

    LOG("tx done\r\n");
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
         CRCDI_L = *(pkt_data + i);
    pkt_desc.typed.header.typed.pay_chksum = CRCINIRES & 0x0f;

    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = (uint16_t)pkt_desc.typed.header.raw;
    __delay_cycles(2); // word CRC takes 2 cycles, so need to delay by at least 1
    pkt_desc.typed.header.typed.hdr_chksum = CRCINIRES & 0x0f;

    LOG("chksum: hdr %02x payload %02x\r\n",
        pkt_desc.typed.header.typed.hdr_chksum,
        pkt_desc.typed.header.typed.pay_chksum);

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
    if (!flash_write(pkt_saved, pkt_data, len)) {
        return FLASH_STATUS_WRITE_FAILED;
    }
    if (!flash_write(desc_addr, (uint8_t *)&pkt_desc.raw, PAYLOAD_DESC_SIZE)) {
        return FLASH_STATUS_WRITE_FAILED;
    }

    return FLASH_STATUS_OK;
}

static bool is_pkt_header_valid(pkt_header_union_t *hdr)
{
    CRCINIRES = 0xFFFF; // init value for checksum
    CRCDI = (uint16_t)PKT_HDR_DATA(hdr->raw);
    __delay_cycles(2); // word takes 2 cycles, so need to delayat least by 1
    unsigned hdr_chksum = CRCINIRES & 0x0f;
    if (hdr_chksum != hdr->typed.hdr_chksum) {
        LOG("pkt header checksum mismatch (%02x != %02x): igoring pkt\r\n",
            hdr_chksum, hdr->typed.hdr_chksum);
        return false;
    }
    LOG("pkt header valid\r\n");
    return true;
}

bool transmit_saved_payload()
{
    // Start at last pkt written, and walk backwards (to the left)
    LOG("look for unsent pkt in flash\r\n");
    uint8_t *prev_saved_pkt_desc_addr = flash_find_last_byte();
    LOG("last byte @0x%04x\r\n", (uint16_t)prev_saved_pkt_desc_addr);
    if (!prev_saved_pkt_desc_addr) {
        LOG("no saved pkt found in flash\r\n");
        return false;
    }
    prev_saved_pkt_desc_addr -= PAYLOAD_DESC_SIZE - 1; // move to first byte of the pkt descriptor
    LOG("pkt desc @0x%04x\r\n", (uint16_t)prev_saved_pkt_desc_addr);

    uint8_t *saved_pkt_desc_addr = NULL;
    uint8_t *saved_pkt_addr = NULL;
    pkt_desc_t saved_pkt_desc;
    pkt_header_t saved_pkt_header;

    do {

        // consider the prev pkt

        pkt_desc_union_t prev_saved_pkt_desc_union;
        prev_saved_pkt_desc_union.raw = AS_PKT_DESC(prev_saved_pkt_desc_addr); // read from flash
        pkt_desc_t prev_saved_pkt_desc = prev_saved_pkt_desc_union.typed;
        pkt_header_t prev_saved_pkt_header = prev_saved_pkt_desc.header.typed;

        LOG("consider pkt desc: %04x %04x: type %u size %u pad %u | chksum: payload %x hdr %x\r\n",
            (uint16_t)(prev_saved_pkt_desc_union.raw >> 16), (uint16_t)(prev_saved_pkt_desc_union.raw & 0xFFFF),
            prev_saved_pkt_header.type, prev_saved_pkt_header.size, prev_saved_pkt_header.padded,
            prev_saved_pkt_header.pay_chksum, prev_saved_pkt_header.hdr_chksum);

        if (!is_pkt_header_valid(&prev_saved_pkt_desc.header)) {
            LOG("reached invalid pkt header\r\n");
            break;
        }

        if (!prev_saved_pkt_desc.sent_mask) {
            LOG("reached sent pkt\r\n");
            break;
        }

        // pkt header is valid and pkt is not sent, set curser on it and keep walking
        LOG("pkt descriptor valid: addr 0x%04x desc\r\n", (uint16_t)prev_saved_pkt_desc_addr);
        saved_pkt_desc_addr = prev_saved_pkt_desc_addr;
        saved_pkt_desc = prev_saved_pkt_desc;
        saved_pkt_header = prev_saved_pkt_header;
        saved_pkt_addr = ((uint8_t*)saved_pkt_desc_addr) - saved_pkt_header.size - saved_pkt_header.padded;

        prev_saved_pkt_desc_addr = saved_pkt_addr - PAYLOAD_DESC_SIZE;
        LOG("prev pkt desc addr: 0x%04x\r\n", (uint16_t)prev_saved_pkt_desc_addr);

    } while (flash_addr_in_range(prev_saved_pkt_desc_addr));

    if (saved_pkt_desc_addr == NULL) {
        LOG("no valid unsent pkt found in flash\r\n");
        return false;
    }

    LOG("pkt payload (addr 0x%04x len %u): ", (uint16_t)saved_pkt_addr, saved_pkt_header.size);
    for(int i = 0; i < saved_pkt_header.size; ++i) {
        LOG("%02x ", *(saved_pkt_addr + i));
    }
    LOG("\r\n");

    CRCINIRES = 0xFFFF; // init value for checksum
    for(int i = 0; i < saved_pkt_header.size; ++i) {
        CRCDI_L = *(saved_pkt_addr + i);
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
    if (!payload_send_pkt(&pkt)) {
        LOG("failed to send pkt\r\n");
        return false;
    }

    saved_pkt_desc.sent_mask &= ~(1 << (15 - sent_mask_offset - unsent_byte_idx));
    LOG("markig pkt at 0x%04x as sent: sent mask 0x%04x\r\n", (uint16_t)saved_pkt_desc_addr, saved_pkt_desc.sent_mask);
    flash_write_word((uint16_t *)saved_pkt_desc_addr, saved_pkt_desc.sent_mask);
    LOG("updated sent mask: @0x%04x [0x%04x]\r\n", (uint16_t)saved_pkt_desc_addr, *(uint16_t *)saved_pkt_desc_addr);

    return true;
}
