# SPDX-License-Identifier: Apache-2.0
"""Build the authorization fixture from the pinned Zephyr USB implementation."""

import sys
from pathlib import Path

from test_nkro_boot import NkroBootTests


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    source = (root / "dependencies/zephyr/subsys/usb/device/usb_device.c").read_text()
    fixture = [
        "#include <stdbool.h>",
        "#include <stddef.h>",
        "#include <errno.h>",
        "#include <zephyr/drivers/usb/usb_dc.h>",
        "#define IS_ENABLED(value) (value)",
        "#define CONFIG_USB_DEVICE_REMOTE_WAKEUP 1",
        "#define LOG_DBG(...)",
        "#include <assert.h>",
        "#define LOG_ERR(...)",
        "#define LOG_WRN(...)",
        "#define __ASSERT_NO_MSG(value) assert(value)",
        "#define MIN(a,b) ((a) < (b) ? (a) : (b))",
        "#define CONFIG_USB_REQUEST_BUFFER_SIZE 128",
        "#define CONFIG_USB_NUMOF_EP_WRITE_RETRIES 0",
        "#define MAX_NUM_REQ_HANDLERS 4",
        "#define USB_CONTROL_EP_OUT 0",
        "#define USB_CONTROL_EP_IN 0x80",
        "#define sys_le16_to_cpu(value) (value)",
        "static void k_yield(void) {}",
        "struct usb_setup_packet { union { uint8_t bmRequestType;",
        "struct { uint8_t recipient:5, type:2, direction:1; } RequestType; };",
        "uint8_t bRequest; uint16_t wValue, wIndex, wLength; };",
        "struct __attribute__((packed)) usb_setup_packet_packed { uint8_t bmRequestType, bRequest; uint16_t wValue, wIndex, wLength; };",
        "typedef int (*usb_request_handler)(struct usb_setup_packet *, int32_t *, uint8_t **);",
        "enum { USB_SREQ_GET_STATUS=0, USB_SREQ_CLEAR_FEATURE=1, USB_SREQ_SET_FEATURE=3,",
        "USB_SREQ_SET_ADDRESS=5, USB_SREQ_GET_DESCRIPTOR=6, USB_SREQ_GET_CONFIGURATION=8,",
        "USB_SREQ_SET_CONFIGURATION=9, USB_SFS_REMOTE_WAKEUP=1 };",
        "enum { USB_DESC_DEVICE=1, USB_DESC_CONFIGURATION=2, USB_DESC_STRING=3,",
        "USB_DESC_INTERFACE=4, USB_DESC_ENDPOINT=5, USB_DESC_OTHER_SPEED=7,",
        "DESC_bLength=0, DESC_bDescriptorType=1, CONF_DESC_wTotalLength=2 };",
        "#define USB_GET_DESCRIPTOR_TYPE(value) ((uint8_t)((value) >> 8))",
        "#define USB_GET_DESCRIPTOR_INDEX(value) ((uint8_t)(value))",
        "static struct { bool remote_wakeup; uint8_t configuration;",
        "struct usb_setup_packet setup; uint8_t *data_buf; int32_t data_buf_residue, data_buf_len;",
        "bool zlp_flag; uint8_t mps0; uint8_t before[16], req_data[128], after[16];",
        "usb_request_handler req_handlers[4]; const uint8_t *descriptors; } usb_dev;",
        "static bool usb_reqtype_is_to_host(struct usb_setup_packet *setup) { return (setup->bmRequestType & 0x80) != 0; }",
        "static bool usb_reqtype_is_to_device(struct usb_setup_packet *setup) { return !usb_reqtype_is_to_host(setup); }",
        "static void usb_print_setup(struct usb_setup_packet *setup) { (void)setup; }",
        "static bool usb_get_status(struct usb_setup_packet *setup, int32_t *length, uint8_t **data) {",
        "(void)setup; (void)length; (void)data; return false; }",
        "static bool usb_set_configuration(struct usb_setup_packet *setup) { (void)setup; return false; }",
        NkroBootTests.function(source, "bool usb_get_remote_wakeup_status(void)"),
        NkroBootTests.function(source, "int usb_wakeup_request(void)"),
    ]
    for function in (
        "static bool usb_get_descriptor(",
        "static bool usb_handle_std_device_req(",
        "int usb_write(",
        "static bool usb_handle_request(",
        "static void usb_data_to_host(",
        "static void usb_handle_control_transfer(",
    ):
        begin = source.index(function)
        signature = source[begin : source.index("{", begin)].strip()
        fixture.append(NkroBootTests.function(source, signature))
    Path(sys.argv[1]).write_text("\n".join(fixture))


if __name__ == "__main__":
    main()
