/*
 * Copyright (C) 2021 Westberry Technology (ChangZhou) Corp., Ltd
 * Copyright (c) 2026 GMK70 contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * WB32 USB register sequences informed by Westberry's Apache-2.0 ChibiOS
 * USBv1 driver (ChibiOS-Contrib 5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767)
 * and WB32FQ95xx reference manual, document 2905025 revision 01, chapter 21.
 * This is the legacy Zephyr usb_dc interface, not the newer UDC interface.
 */
#define DT_DRV_COMPAT westberry_wb32_usb

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/usb/usb_dc.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <soc.h>

#define USB_BASE_ADDR DT_INST_REG_ADDR(0)
#define WB32_EP_COUNT 4U
#define WB32_PACKET_SIZE 64U
#define EP_IN 0x80U
#define EP_NUM(ep) ((ep) & 0x0fU)
#define FADDR 0x00U
#define POWER 0x01U
#define INTRIN 0x02U
#define INTROUT 0x04U
#define INTRUSB 0x06U
#define INTRINE 0x07U
#define INTROUTE 0x09U
#define INTRUSBE 0x0bU
#define INDEX 0x0eU
#define INMAXP 0x10U
#define CSR0 0x11U
#define INCSR1 0x11U
#define INCSR2 0x12U
#define OUTMAXP 0x13U
#define OUTCSR1 0x14U
#define OUTCSR2 0x15U
#define COUNTL 0x16U
#define COUNTH 0x17U
#define FIFO(ep) (0x20U + 4U * (ep))
#define CSR0_OUT_READY 0x01U
#define CSR0_IN_READY 0x02U
#define CSR0_SENT_STALL 0x04U
#define CSR0_DATA_END 0x08U
#define CSR0_SETUP_END 0x10U
#define CSR0_SEND_STALL 0x20U
#define CSR0_ACK_OUT 0x40U
#define CSR0_ACK_SETUP_END 0x80U
#define IN_READY 0x01U
#define IN_FLUSH 0x08U
#define IN_STALL 0x10U
#define IN_SENT_STALL 0x20U
#define IN_CLEAR_TOGGLE 0x40U
#define OUT_READY 0x01U
#define OUT_FLUSH 0x10U
#define OUT_STALL 0x20U
#define OUT_SENT_STALL 0x40U
#define OUT_CLEAR_TOGGLE 0x80U
#define USB_SUSPEND 0x01U
#define USB_RESUME 0x02U
#define USB_RESET 0x04U
#define USB_SOF 0x08U

struct wb32_endpoint {
    usb_dc_ep_callback callback;
    uint16_t           mps;
    bool               configured;
    bool               enabled;
    bool               stalled;
    bool               tx_busy;
    bool               rx_ready;
    bool               rx_setup;
    bool               rx_final;
    uint8_t            rx_length;
    uint8_t            rx_offset;
};

static struct wb32_endpoint endpoints[2][WB32_EP_COUNT];
/* IN endpoints write the hardware FIFO directly; only OUT packets need RAM. */
static uint8_t                rx_buffers[WB32_EP_COUNT][WB32_PACKET_SIZE];
static usb_dc_status_callback status_callback;
static bool                   attached;
static enum { EP0_SETUP, EP0_DATA_IN, EP0_DATA_OUT, EP0_STATUS_IN, EP0_STATUS_OUT } control_phase;
static uint16_t control_remaining;
static bool     control_final_in;

static uint8_t reg_read(unsigned int offset) {
    return sys_read8(USB_BASE_ADDR + offset);
}
static void reg_write(unsigned int offset, uint8_t value) {
    sys_write8(value, USB_BASE_ADDR + offset);
}

static bool valid_endpoint(uint8_t ep) {
    return !(ep & 0x70U) && EP_NUM(ep) < WB32_EP_COUNT;
}
static struct wb32_endpoint *endpoint(uint8_t ep) {
    return &endpoints[(ep & EP_IN) != 0][EP_NUM(ep)];
}

static void notify_status(enum usb_dc_status_code status) {
    if (status_callback) status_callback(status, NULL);
}

static void notify_endpoint(uint8_t ep, enum usb_dc_ep_cb_status_code status) {
    if (endpoint(ep)->callback) endpoint(ep)->callback(ep, status);
}

static void reset_control(void) {
    control_phase            = EP0_SETUP;
    control_remaining        = 0;
    control_final_in         = false;
    endpoints[0][0].rx_ready = false;
    endpoints[0][0].stalled  = false;
    endpoints[1][0].tx_busy  = false;
    endpoints[1][0].stalled  = false;
}

/* Caller holds the interrupt lock. Flush twice: nonzero FIFOs double buffer. */
static void flush_endpoint(uint8_t ep) {
    unsigned int          number = EP_NUM(ep);
    struct wb32_endpoint *state  = endpoint(ep);
    reg_write(INDEX, number);
    if (number) {
        unsigned int csr     = (ep & EP_IN) ? INCSR1 : OUTCSR1;
        uint8_t      command = (ep & EP_IN) ? IN_FLUSH : OUT_FLUSH;
        if (state->stalled) command |= (ep & EP_IN) ? IN_STALL : OUT_STALL;
        reg_write(csr, command);
        reg_write(csr, command);
    }
    state->tx_busy  = false;
    state->rx_ready = false;
}

static void reset_controller(void) {
    reset_control();
    reg_write(FADDR, 0);
    reg_write(INTRINE, 1);
    reg_write(INTROUTE, 0);
    reg_write(INTRUSBE, USB_RESET | USB_RESUME | USB_SUSPEND);
    reg_write(POWER, 1); /* SUSEN; leave clocks active to detect resume. */
    for (unsigned int number = 0; number < WB32_EP_COUNT; ++number) {
        for (unsigned int dir = 0; dir < 2; ++dir) {
            struct wb32_endpoint *state = &endpoints[dir][number];
            state->stalled = state->tx_busy = state->rx_ready = false;
            state->enabled                                    = number == 0;
            if (number) {
                flush_endpoint(number | (dir ? EP_IN : 0));
            } else {
                state->mps        = WB32_PACKET_SIZE;
                state->configured = true;
            }
        }
    }
    reg_write(INDEX, 0);
    reg_write(CSR0, 0);
}

/* Buffer one packet and hold OUTPKTRDY until read_continue supplies backpressure. */
static bool receive_packet(unsigned int number, bool setup) {
    struct wb32_endpoint *state  = &endpoints[0][number];
    unsigned int          length = reg_read(COUNTL);
    if (number) length |= (unsigned int)reg_read(COUNTH) << 8;
    if (length > state->mps || length > sizeof(rx_buffers[number]) || (setup && length != 8U) || (!number && !setup && control_phase == EP0_DATA_OUT && length > control_remaining)) {
        /* Drain malformed data without ever indexing outside the software FIFO. */
        for (unsigned int i = 0; i < length; ++i)
            (void)reg_read(FIFO(number));
        (void)usb_dc_ep_set_stall(number);
        notify_status(USB_DC_ERROR);
        return false;
    }
    for (unsigned int i = 0; i < length; ++i)
        rx_buffers[number][i] = reg_read(FIFO(number));
    state->rx_length = length;
    state->rx_offset = 0;
    state->rx_ready  = true;
    state->rx_setup  = setup;
    state->rx_final  = false;
    if (setup) {
        control_remaining = (uint16_t)rx_buffers[number][6] | ((uint16_t)rx_buffers[number][7] << 8);
        control_phase     = !control_remaining ? EP0_STATUS_IN : (rx_buffers[number][0] & EP_IN) ? EP0_DATA_IN : EP0_DATA_OUT;
    } else if (!number && control_phase == EP0_DATA_OUT) {
        control_remaining -= length;
        state->rx_final = !control_remaining || length < state->mps;
    }
    return true;
}

static void service_ep0(void) {
    reg_write(INDEX, 0);
    uint8_t csr = reg_read(CSR0);
    if (csr & CSR0_SENT_STALL) {
        reg_write(CSR0, 0);
        reset_control();
    }
    if (csr & CSR0_SETUP_END) {
        reg_write(CSR0, CSR0_ACK_SETUP_END);
        reset_control();
    }
    struct wb32_endpoint *out = &endpoints[0][0];
    struct wb32_endpoint *in  = &endpoints[1][0];
    if ((csr & CSR0_OUT_READY) && !out->rx_ready) {
        /* A completed IN transfer may be followed immediately by a new SETUP. */
        bool setup = control_phase == EP0_SETUP || ((control_phase == EP0_STATUS_OUT || (control_phase == EP0_STATUS_IN && in->tx_busy)) && reg_read(COUNTL) == 8);
        if (setup) {
            reset_control();
            if (receive_packet(0, true)) notify_endpoint(0, USB_DC_EP_SETUP);
            return;
        }
    }
    if (in->tx_busy && !(csr & CSR0_IN_READY)) {
        in->tx_busy = false;
        if (control_phase == EP0_STATUS_IN) control_phase = EP0_SETUP;
        notify_endpoint(EP_IN, USB_DC_EP_DATA_IN);
    } else if (!in->tx_busy && control_phase == EP0_STATUS_OUT && !(csr & CSR0_OUT_READY)) {
        control_phase = EP0_SETUP; /* Hardware consumed the OUT status handshake. */
    }
    if ((csr & CSR0_OUT_READY) && !out->rx_ready && (control_phase == EP0_DATA_OUT || control_phase == EP0_STATUS_OUT)) {
        reg_write(INDEX, 0); /* An IN callback may have selected another endpoint. */
        if (receive_packet(0, false)) notify_endpoint(0, USB_DC_EP_DATA_OUT);
    }
}

static void wb32_usb_isr(const void *arg) {
    (void)arg;
    /* Each flag register is read-to-clear: take exactly one snapshot of each. */
    uint8_t usb         = reg_read(INTRUSB);
    uint8_t in          = reg_read(INTRIN);
    uint8_t out         = reg_read(INTROUT);
    uint8_t saved_index = reg_read(INDEX);
    if (usb & USB_RESET) {
        reset_controller();
        notify_status(USB_DC_RESET);
        /* Endpoint flags from before RESET cannot complete old transfers. */
        in = out = 0;
    }
    if (usb & USB_RESUME) notify_status(USB_DC_RESUME);
    if (in & 1) service_ep0();
    for (unsigned int number = 1; number < WB32_EP_COUNT; ++number) {
        struct wb32_endpoint *tx = &endpoints[1][number];
        struct wb32_endpoint *rx = &endpoints[0][number];
        reg_write(INDEX, number);
        if ((in & (1U << number)) && tx->enabled) {
            uint8_t csr = reg_read(INCSR1);
            if (csr & IN_SENT_STALL) reg_write(INCSR1, csr & ~IN_SENT_STALL);
            if (tx->tx_busy && !(csr & IN_READY)) {
                tx->tx_busy = false;
                notify_endpoint(number | EP_IN, USB_DC_EP_DATA_IN);
            }
        }
        /* A callback may have changed INDEX. */
        reg_write(INDEX, number);
        if ((out & (1U << number)) && rx->enabled && !rx->rx_ready) {
            uint8_t csr = reg_read(OUTCSR1);
            if (csr & OUT_SENT_STALL) reg_write(OUTCSR1, csr & ~OUT_SENT_STALL);
            if ((csr & OUT_READY) && receive_packet(number, false)) notify_endpoint(number, USB_DC_EP_DATA_OUT);
        }
    }
    if (usb & USB_SOF) notify_status(USB_DC_SOF);
    if (usb & USB_SUSPEND) notify_status(USB_DC_SUSPEND);
    reg_write(INDEX, saved_index);
}

int usb_dc_attach(void) {
    unsigned int key = irq_lock();
    if (!attached) {
        wb32_usb_clock_enable();
        IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), wb32_usb_isr, NULL, 0);
        attached = true;
        reset_controller();
        /* Discard stale flags left by the previous application. */
        (void)reg_read(INTRUSB);
        (void)reg_read(INTRIN);
        (void)reg_read(INTROUT);
        irq_enable(DT_INST_IRQN(0));
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_detach(void) {
    unsigned int key          = irq_lock();
    bool         was_attached = attached;
    if (attached) {
        irq_disable(DT_INST_IRQN(0));
        reg_write(INTRUSBE, 0);
        reg_write(INTRINE, 0);
        reg_write(INTROUTE, 0);
        wb32_usb_disconnect();
        wb32_usb_clock_disable();
        attached = false;
        reset_control();
        for (unsigned int dir = 0; dir < 2; ++dir) {
            for (unsigned int number = 0; number < WB32_EP_COUNT; ++number)
                endpoints[dir][number].enabled = false;
        }
    }
    irq_unlock(key);
    if (was_attached) notify_status(USB_DC_DISCONNECTED);
    return 0;
}

int usb_dc_reset(void) {
    unsigned int key = irq_lock();
    if (attached) reset_controller();
    irq_unlock(key);
    return 0;
}

int usb_dc_set_address(const uint8_t addr) {
    if (addr > 127U) return -EINVAL;
    unsigned int key = irq_lock();
    if (!attached) {
        irq_unlock(key);
        return -ENODEV;
    }
    /* FADDR.UPDATE defers this address until the current status transfer ends. */
    reg_write(FADDR, addr);
    irq_unlock(key);
    return 0;
}

void usb_dc_set_status_callback(const usb_dc_status_callback cb) {
    status_callback = cb;
}

int usb_dc_ep_check_cap(const struct usb_dc_ep_cfg_data *const cfg) {
    if (!cfg || !valid_endpoint(cfg->ep_addr) || !cfg->ep_mps || cfg->ep_mps > WB32_PACKET_SIZE) return -EINVAL;
    if (cfg->ep_type == USB_DC_EP_ISOCHRONOUS) return -ENOTSUP;
    if (!EP_NUM(cfg->ep_addr)) return cfg->ep_type == USB_DC_EP_CONTROL && cfg->ep_mps == WB32_PACKET_SIZE ? 0 : -EINVAL;
    if (cfg->ep_type == USB_DC_EP_INTERRUPT) return 0;
    if (cfg->ep_type == USB_DC_EP_BULK && (cfg->ep_mps == 8 || cfg->ep_mps == 16 || cfg->ep_mps == 32 || cfg->ep_mps == 64)) return 0;
    return -EINVAL;
}

int usb_dc_ep_configure(const struct usb_dc_ep_cfg_data *const cfg) {
    int ret = usb_dc_ep_check_cap(cfg);
    if (ret) return ret;
    unsigned int key = irq_lock();
    if (!attached) {
        irq_unlock(key);
        return -ENODEV;
    }
    struct wb32_endpoint *state = endpoint(cfg->ep_addr);
    state->mps                  = cfg->ep_mps;
    state->configured           = true;
    state->stalled              = false;
    state->tx_busy              = false;
    state->rx_ready             = false;
    reg_write(INDEX, EP_NUM(cfg->ep_addr));
    if (EP_NUM(cfg->ep_addr)) {
        bool in = cfg->ep_addr & EP_IN;
        reg_write(in ? INMAXP : OUTMAXP, (cfg->ep_mps + 7U) / 8U);
        reg_write(in ? INCSR2 : OUTCSR2, 0); /* No AUTOSET/AUTOCLR or ISO. */
        flush_endpoint(cfg->ep_addr);
        reg_write(in ? INCSR1 : OUTCSR1, in ? IN_CLEAR_TOGGLE : OUT_CLEAR_TOGGLE);
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_set_callback(const uint8_t ep, const usb_dc_ep_callback cb) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int key       = irq_lock();
    endpoint(ep)->callback = cb;
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_enable(const uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int          key   = irq_lock();
    struct wb32_endpoint *state = endpoint(ep);
    if (!attached || !state->configured) {
        irq_unlock(key);
        return -ENODEV;
    }
    state->enabled    = true;
    unsigned int mask = (!EP_NUM(ep) || (ep & EP_IN)) ? INTRINE : INTROUTE;
    reg_write(mask, reg_read(mask) | (1U << EP_NUM(ep)));
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_disable(const uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int          key   = irq_lock();
    struct wb32_endpoint *state = endpoint(ep);
    state->enabled              = false;
    state->tx_busy = state->rx_ready = false;
    if (attached) {
        unsigned int mask = (!EP_NUM(ep) || (ep & EP_IN)) ? INTRINE : INTROUTE;
        if (EP_NUM(ep) || !endpoint(ep ^ EP_IN)->enabled) reg_write(mask, reg_read(mask) & ~(1U << EP_NUM(ep)));
        flush_endpoint(ep);
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_flush(const uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int key = irq_lock();
    if (!attached) {
        irq_unlock(key);
        return -ENODEV;
    }
    if (!EP_NUM(ep)) {
        reset_control();
        reg_write(INDEX, 0);
        reg_write(CSR0, CSR0_ACK_OUT | CSR0_ACK_SETUP_END);
    } else
        flush_endpoint(ep);
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_set_stall(const uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int key = irq_lock();
    if (!attached) {
        irq_unlock(key);
        return -ENODEV;
    }
    endpoint(ep)->stalled = true;
    endpoint(ep)->tx_busy = false;
    reg_write(INDEX, EP_NUM(ep));
    if (!EP_NUM(ep)) {
        endpoints[0][0].stalled = endpoints[1][0].stalled = true;
        reg_write(CSR0, CSR0_ACK_OUT | CSR0_SEND_STALL);
    } else
        reg_write((ep & EP_IN) ? INCSR1 : OUTCSR1, (ep & EP_IN) ? IN_STALL : OUT_STALL);
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_clear_stall(const uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    unsigned int key = irq_lock();
    if (!attached) {
        irq_unlock(key);
        return -ENODEV;
    }
    endpoint(ep)->stalled = false;
    reg_write(INDEX, EP_NUM(ep));
    if (!EP_NUM(ep)) {
        reset_control();
        reg_write(CSR0, 0);
    } else {
        flush_endpoint(ep);
        reg_write((ep & EP_IN) ? INCSR1 : OUTCSR1, (ep & EP_IN) ? IN_CLEAR_TOGGLE : OUT_CLEAR_TOGGLE);
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_is_stalled(const uint8_t ep, uint8_t *const stalled) {
    if (!valid_endpoint(ep) || !stalled) return -EINVAL;
    *stalled = endpoint(ep)->stalled;
    return 0;
}

int usb_dc_ep_halt(const uint8_t ep) {
    return usb_dc_ep_set_stall(ep);
}

int usb_dc_ep_write(const uint8_t ep, const uint8_t *const data, const uint32_t data_len, uint32_t *const ret_bytes) {
    if (!valid_endpoint(ep) || !(ep & EP_IN) || (data_len && !data)) return -EINVAL;
    if (ret_bytes) *ret_bytes = 0;
    unsigned int          key   = irq_lock();
    struct wb32_endpoint *state = endpoint(ep);
    if (!attached || !state->enabled) {
        irq_unlock(key);
        return -ENODEV;
    }
    if (state->stalled) {
        irq_unlock(key);
        return -EPIPE;
    }
    if (state->tx_busy) {
        irq_unlock(key);
        return -EAGAIN;
    }
    if (data_len > state->mps && !ret_bytes) {
        irq_unlock(key);
        return -EMSGSIZE;
    }
    uint32_t     length = data_len > state->mps ? state->mps : data_len;
    unsigned int number = EP_NUM(ep);
    reg_write(INDEX, number);
    if (reg_read(INCSR1) & (number ? IN_READY : CSR0_IN_READY)) {
        irq_unlock(key);
        return -EAGAIN;
    }
    uint8_t command;
    if (!number) {
        if (control_phase == EP0_STATUS_IN) {
            if (length) {
                irq_unlock(key);
                return -EINVAL;
            }
            /* No-data and OUT requests use the core's automatic IN status. */
            command = CSR0_ACK_OUT | CSR0_DATA_END;
        } else if (control_phase == EP0_DATA_IN) {
            if (length > control_remaining) {
                irq_unlock(key);
                return -EINVAL;
            }
            control_remaining -= length;
            control_final_in = !control_remaining || length < state->mps;
            command          = CSR0_IN_READY | (control_final_in ? CSR0_DATA_END : 0);
            if (control_final_in) control_phase = EP0_STATUS_OUT;
        } else {
            irq_unlock(key);
            return -EINVAL;
        }
    } else
        command = IN_READY;
    for (uint32_t i = 0; i < length; ++i)
        reg_write(FIFO(number), data[i]);
    state->tx_busy = true;
    reg_write(number ? INCSR1 : CSR0, command);
    if (ret_bytes) *ret_bytes = length;
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_read_wait(uint8_t ep, uint8_t *data, uint32_t max_data_len, uint32_t *read_bytes) {
    if (!valid_endpoint(ep) || (ep & EP_IN) || (max_data_len && !data)) return -EINVAL;
    if (read_bytes) *read_bytes = 0;
    unsigned int          key   = irq_lock();
    struct wb32_endpoint *state = endpoint(ep);
    if (!attached || !state->enabled) {
        irq_unlock(key);
        return -ENODEV;
    }
    if (!state->rx_ready) {
        irq_unlock(key);
        return -EAGAIN;
    }
    uint32_t length = state->rx_length - state->rx_offset;
    if (!data && !max_data_len) {
        if (read_bytes) *read_bytes = length;
    } else {
        if (length > max_data_len) length = max_data_len;
        if (length) memcpy(data, rx_buffers[EP_NUM(ep)] + state->rx_offset, length);
        state->rx_offset += length;
        if (read_bytes) *read_bytes = length;
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_read_continue(uint8_t ep) {
    if (!valid_endpoint(ep) || (ep & EP_IN)) return -EINVAL;
    unsigned int          key   = irq_lock();
    struct wb32_endpoint *state = endpoint(ep);
    if (!attached || !state->enabled) {
        irq_unlock(key);
        return -ENODEV;
    }
    if (state->rx_ready && state->rx_offset == state->rx_length) {
        reg_write(INDEX, EP_NUM(ep));
        if (EP_NUM(ep))
            reg_write(OUTCSR1, state->stalled ? OUT_STALL : 0);
        else if (state->rx_setup && control_phase == EP0_STATUS_IN) {
            /* Keep SETUP pending until the stack accepts or stalls the request. */
        } else if (state->rx_final) {
            control_phase = EP0_STATUS_IN; /* Defer DATAEND until status write. */
        } else {
            reg_write(CSR0, CSR0_ACK_OUT);
            if (control_phase == EP0_STATUS_OUT) control_phase = EP0_SETUP;
        }
        state->rx_ready = false;
    }
    irq_unlock(key);
    return 0;
}

int usb_dc_ep_read(const uint8_t ep, uint8_t *const data, const uint32_t max_data_len, uint32_t *const read_bytes) {
    int ret = usb_dc_ep_read_wait(ep, data, max_data_len, read_bytes);
    if (!ret && data) ret = usb_dc_ep_read_continue(ep);
    return ret;
}

int usb_dc_ep_mps(uint8_t ep) {
    if (!valid_endpoint(ep)) return -EINVAL;
    return endpoint(ep)->mps;
}

/* The first awake USB-only port does not advertise remote wakeup. */
int usb_dc_wakeup_request(void) {
    return -ENOTSUP;
}
