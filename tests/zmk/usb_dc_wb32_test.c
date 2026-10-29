/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/usb/usb_dc.h>
#include "../../firmware/zmk/drivers/usb/device/usb_dc_wb32.c"

void (*fake_usb_isr)(const void *);
static uint8_t  common[16], indexed[4][8], rx_fifo[4][128], tx_fifo[4][128];
static unsigned rx_pos[4], tx_len[4], rx_len[4], clocks, disconnects;
static unsigned ep_events[256][3], status_events[16], dataends, flushes[4][2];
static uint8_t  last_csr0_write;

void wb32_usb_clock_enable(void) {
    ++clocks;
}
void wb32_usb_disconnect(void) {
    ++disconnects;
}
void wb32_usb_clock_disable(void) {}

uint8_t sys_read8(uintptr_t address) {
    unsigned offset = address - 0x40014000UL;
    unsigned ep     = common[14];
    assert(offset < 0x30);
    if (offset >= 0x20) {
        ep = (offset - 0x20) / 4;
        assert(rx_pos[ep] < rx_len[ep]);
        return rx_fifo[ep][rx_pos[ep]++];
    }
    if (offset >= 0x10) return indexed[ep][offset - 0x10];
    uint8_t value = common[offset];
    if (offset == 2 || offset == 4 || offset == 6) common[offset] = 0;
    return value;
}

void sys_write8(uint8_t value, uintptr_t address) {
    unsigned offset = address - 0x40014000UL;
    unsigned ep     = common[14];
    assert(offset < 0x30);
    if (offset >= 0x20) {
        ep = (offset - 0x20) / 4;
        assert(tx_len[ep] < sizeof(tx_fifo[ep]));
        tx_fifo[ep][tx_len[ep]++] = value;
    } else if (offset == 0x11 && ep == 0) {
        uint8_t csr     = indexed[0][1] & 0x13;
        last_csr0_write = value;
        if (value & 0x80) csr &= ~0x10;
        if (value & 0x40) csr &= ~1;
        if (value & 8) ++dataends;
        indexed[0][1] = csr | (value & 0x22);
    } else if (offset >= 0x10) {
        if (ep && offset == 0x11 && (value & 8)) {
            ++flushes[ep][1];
            tx_len[ep] = 0;
        }
        if (ep && offset == 0x14 && (value & 0x10)) ++flushes[ep][0];
        indexed[ep][offset - 0x10] = value;
    } else {
        common[offset] = value;
    }
}

static void ep_callback(uint8_t ep, enum usb_dc_ep_cb_status_code event) {
    ++ep_events[ep][event];
}
static void host_status_callback(enum usb_dc_status_code event, const uint8_t *param) {
    (void)param;
    ++status_events[event];
}

static void configure(uint8_t address, uint16_t mps, enum usb_dc_ep_transfer_type type) {
    struct usb_dc_ep_cfg_data cfg = {.ep_addr = address, .ep_mps = mps, .ep_type = type};
    assert(usb_dc_ep_configure(&cfg) == 0);
    assert(usb_dc_ep_set_callback(address, ep_callback) == 0);
    assert(usb_dc_ep_enable(address) == 0);
}

static void start(void) {
    (void)usb_dc_detach();
    memset(common, 0, sizeof(common));
    memset(indexed, 0, sizeof(indexed));
    memset(rx_len, 0, sizeof(rx_len));
    memset(tx_len, 0, sizeof(tx_len));
    memset(ep_events, 0, sizeof(ep_events));
    memset(status_events, 0, sizeof(status_events));
    memset(flushes, 0, sizeof(flushes));
    dataends = clocks = disconnects = 0;
    usb_dc_set_status_callback(host_status_callback);
    assert(usb_dc_attach() == 0);
    assert(clocks == 1);
    assert(fake_usb_isr != NULL);
    configure(0, 64, USB_DC_EP_CONTROL);
    configure(0x80, 64, USB_DC_EP_CONTROL);
}

static void receive(unsigned ep, const uint8_t *data, unsigned length) {
    assert(length <= 128);
    if (length) memcpy(rx_fifo[ep], data, length);
    rx_pos[ep]     = 0;
    rx_len[ep]     = length;
    indexed[ep][6] = length;
    indexed[ep][7] = 0;
    indexed[ep][ep ? 4 : 1] |= 1;
    common[ep ? 4 : 2] |= 1U << ep;
    fake_usb_isr(NULL);
}

static void setup(bool in, uint16_t length) {
    uint8_t packet[8] = {in ? 0x80 : 0x00, 6, 0, 1, 0, 0, length & 255, length >> 8};
    receive(0, packet, sizeof(packet));
    assert(ep_events[0][USB_DC_EP_SETUP]);
    uint8_t result[8];
    assert(usb_dc_ep_read(0, result, sizeof(result), NULL) == 0);
    assert(memcmp(result, packet, sizeof(packet)) == 0);
}

static void complete_in(unsigned ep) {
    indexed[ep][1] &= ~(ep ? 1 : 2);
    tx_len[ep] = 0;
    common[2]  = 1U << ep;
    fake_usb_isr(NULL);
}

static void test_capabilities_and_lifecycle(void) {
    start();
    struct usb_dc_ep_cfg_data cfg = {.ep_addr = 0x81, .ep_mps = 17, .ep_type = USB_DC_EP_INTERRUPT};
    assert(usb_dc_ep_check_cap(&cfg) == 0);
    cfg.ep_addr = 0x84;
    assert(usb_dc_ep_check_cap(&cfg) == -EINVAL);
    cfg.ep_addr = 0x91;
    assert(usb_dc_ep_check_cap(&cfg) == -EINVAL);
    cfg.ep_addr = 0x81;
    cfg.ep_mps  = 65;
    assert(usb_dc_ep_check_cap(&cfg) == -EINVAL);
    cfg.ep_mps  = 64;
    cfg.ep_type = USB_DC_EP_ISOCHRONOUS;
    assert(usb_dc_ep_check_cap(&cfg) == -ENOTSUP);
    assert(usb_dc_detach() == 0);
    assert(disconnects == 1);
    assert(common[7] == 0 && common[9] == 0 && common[11] == 0);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == -ENODEV);
}

static void test_control_in_packets_and_status(void) {
    start();
    setup(true, 128);
    assert((indexed[0][1] & 1) == 0 && dataends == 0);
    uint8_t bytes[128];
    for (unsigned i = 0; i < sizeof(bytes); ++i)
        bytes[i] = i;
    uint32_t written = 0;
    assert(usb_dc_ep_write(0x80, bytes, sizeof(bytes), &written) == 0 && written == 64);
    assert(tx_len[0] == 64 && memcmp(tx_fifo[0], bytes, 64) == 0 && dataends == 0);
    assert(usb_dc_ep_write(0x80, bytes, 1, NULL) == -EAGAIN);
    complete_in(0);
    assert(ep_events[0x80][USB_DC_EP_DATA_IN] == 1);
    assert(usb_dc_ep_write(0x80, bytes + 64, 64, &written) == 0 && written == 64);
    assert(dataends == 1 && (last_csr0_write & 0x0a) == 0x0a);
    complete_in(0);
    receive(0, NULL, 0);
    assert(ep_events[0][USB_DC_EP_SETUP] == 1);
    assert(usb_dc_ep_read(0, bytes, 0, &written) == 0 && written == 0);
    assert(ep_events[0x80][USB_DC_EP_DATA_IN] == 2);
}

static void test_control_short_response_needs_zlp(void) {
    start();
    setup(true, 128);
    uint8_t packet[64] = {0};
    assert(usb_dc_ep_write(0x80, packet, 64, NULL) == 0 && dataends == 0);
    complete_in(0);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0 && dataends == 1);
    assert((last_csr0_write & 0x0a) == 0x0a);
}

static void test_zero_data_address_uses_hardware_status(void) {
    start();
    setup(false, 0);
    assert(indexed[0][1] & 1);
    assert(usb_dc_set_address(37) == 0 && common[0] == 37);
    assert(usb_dc_set_address(128) == -EINVAL);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0);
    assert(last_csr0_write == 0x48 && dataends == 1 && tx_len[0] == 0);
    complete_in(0);
    assert(ep_events[0x80][USB_DC_EP_DATA_IN] == 1);
}

static void test_control_out_partial_reads_and_deferred_status(void) {
    start();
    setup(false, 128);
    uint8_t bytes[64], result[64];
    for (unsigned i = 0; i < 64; ++i)
        bytes[i] = i + 3;
    receive(0, bytes, 64);
    uint32_t length;
    assert(usb_dc_ep_read_wait(0, NULL, 0, &length) == 0 && length == 64);
    assert(usb_dc_ep_read_wait(0, result, 7, &length) == 0 && length == 7);
    assert(indexed[0][1] & 1);
    assert(usb_dc_ep_read_continue(0) == 0 && (indexed[0][1] & 1));
    assert(usb_dc_ep_read(0, result + 7, 57, &length) == 0 && length == 57);
    assert(memcmp(result, bytes, 64) == 0 && !(indexed[0][1] & 1) && dataends == 0);
    receive(0, bytes, 64);
    assert(usb_dc_ep_read(0, result, 64, &length) == 0 && length == 64);
    assert((indexed[0][1] & 1) && dataends == 0);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0 && dataends == 1);
    assert(last_csr0_write == 0x48);
}

static void test_interrupt_fifo_limits_flush_and_stall(void) {
    start();
    unsigned int previous_flushes = flushes[1][1];
    configure(0x81, 17, USB_DC_EP_INTERRUPT);
    assert(indexed[1][0] == 3 && flushes[1][1] == previous_flushes + 2);
    uint8_t  data[18] = {1, 2, 3};
    uint32_t count;
    assert(usb_dc_ep_write(0x81, data, 18, NULL) == -EMSGSIZE);
    assert(usb_dc_ep_write(0x81, data, 18, &count) == 0 && count == 17);
    assert(tx_len[1] == 17 && memcmp(tx_fifo[1], data, 17) == 0);
    assert(usb_dc_ep_write(0x81, data, 1, &count) == -EAGAIN);
    complete_in(1);
    assert(ep_events[0x81][USB_DC_EP_DATA_IN] == 1);
    assert(usb_dc_ep_set_stall(0x81) == 0);
    uint8_t stalled;
    assert(usb_dc_ep_is_stalled(0x81, &stalled) == 0 && stalled);
    assert(usb_dc_ep_clear_stall(0x81) == 0);
    assert(usb_dc_ep_is_stalled(0x81, &stalled) == 0 && !stalled);
    assert(indexed[1][1] & 0x40);
    assert(usb_dc_ep_disable(0x81) == 0);
    assert(usb_dc_ep_write(0x81, data, 1, &count) == -ENODEV);
}

static void test_setupend_recovers_and_reset_cancels_transfers(void) {
    start();
    setup(true, 128);
    uint8_t data[64] = {0};
    assert(usb_dc_ep_write(0x80, data, 64, NULL) == 0);
    indexed[0][1] = 0x10;
    setup(false, 0);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0);
    configure(0x81, 8, USB_DC_EP_INTERRUPT);
    assert(usb_dc_ep_write(0x81, data, 8, NULL) == 0);
    common[6] = 4;
    common[2] = 3;
    fake_usb_isr(NULL);
    assert(status_events[USB_DC_RESET] == 1 && common[0] == 0);
    assert(common[7] == 1 && common[9] == 0);
    assert(ep_events[0x81][USB_DC_EP_DATA_IN] == 0);
    setup(false, 0);
    assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0);
}

/* A length query must not rearm a ZLP OUT endpoint before its consumer. */
static void test_zero_length_query_keeps_out_backpressure(void) {
    start();
    configure(1, 8, USB_DC_EP_INTERRUPT);
    receive(1, NULL, 0);
    uint32_t count = 99;
    assert(usb_dc_ep_read(1, NULL, 0, &count) == 0 && count == 0);
    assert(indexed[1][4] & 1);
    uint8_t ignored;
    assert(usb_dc_ep_read(1, &ignored, 0, &count) == 0 && count == 0);
    assert(!(indexed[1][4] & 1));
}

/* Coalesced EP0 events must complete the last IN packet before OUT status. */
static void test_coalesced_final_in_and_status_out(void) {
    start();
    setup(true, 1);
    uint8_t data = 7;
    assert(usb_dc_ep_write(0x80, &data, 1, NULL) == 0);
    indexed[0][1] &= ~2U;
    receive(0, NULL, 0);
    assert(ep_events[0x80][USB_DC_EP_DATA_IN] == 1);
    assert(ep_events[0][USB_DC_EP_DATA_OUT] == 1);
}

/* Adding a per-direction packet buffer would exceed the radio RAM budget. */
static void test_endpoint_metadata_does_not_reserve_packet_storage(void) {
    assert(sizeof(endpoints) <= 192U);
    assert(sizeof(rx_buffers) == 256U);
    assert(sizeof(endpoints) + sizeof(rx_buffers) <= 448U);
}

/* Sharing RX storage across endpoints corrupts packets awaiting partial reads. */
static void test_control_and_bulk_out_packets_remain_independent(void) {
    start();
    for (unsigned ep = 1; ep < 4; ++ep) {
        configure(ep, 64, USB_DC_EP_BULK);
        configure(0x80U | ep, 64, USB_DC_EP_BULK);
    }
    setup(false, 64);
    const uint8_t first[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t       packets[4][64], result[64];
    for (unsigned ep = 0; ep < 4; ++ep) {
        memset(packets[ep], first[ep], sizeof(packets[ep]));
        receive(ep, packets[ep], sizeof(packets[ep]));
        uint32_t count = 0;
        assert(usb_dc_ep_read_wait(ep, result, 7, &count) == 0 && count == 7);
        assert(memcmp(result, packets[ep], 7) == 0);
    }
    for (unsigned ep = 0; ep < 4; ++ep) {
        const uint8_t tx[3] = {0xa1, 0xb2, 0xc3};
        if (ep) assert(usb_dc_ep_write(0x80U | ep, tx, sizeof(tx), NULL) == 0);
        uint32_t count = 0;
        assert(usb_dc_ep_read_wait(0x80U | ep, result, sizeof(result), &count) == -EINVAL);
        assert(usb_dc_ep_read_wait(ep, NULL, 0, &count) == 0 && count == 57);
        assert(usb_dc_ep_read(ep, result, sizeof(result), &count) == 0 && count == 57);
        assert(memcmp(result, packets[ep] + 7, 57) == 0);
        if (!ep) assert(usb_dc_ep_write(0x80, NULL, 0, NULL) == 0);
    }
}

int main(void) {
    test_capabilities_and_lifecycle();
    test_control_in_packets_and_status();
    test_control_short_response_needs_zlp();
    test_zero_data_address_uses_hardware_status();
    test_control_out_partial_reads_and_deferred_status();
    test_interrupt_fifo_limits_flush_and_stall();
    test_setupend_recovers_and_reset_cancels_transfers();
    test_zero_length_query_keeps_out_backpressure();
    test_coalesced_final_in_and_status_out();
    test_control_and_bulk_out_packets_remain_independent();
    test_endpoint_metadata_does_not_reserve_packet_storage();
    puts("WB32 USB controller tests passed");
    return 0;
}
