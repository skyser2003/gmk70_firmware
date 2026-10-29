"""Radio image policy and actual instruction/body checks, without an ARM build."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


RADIO_SYMBOLS: frozenset[str] = frozenset(
    {
        "gmk_radio_poll",
        "__wrap_zmk_endpoint_send_report",
        "__wrap_zmk_hid_keyboard_clear",
        "__wrap_zmk_hid_consumer_clear",
        "__wrap_zmk_keymap_set_layer_binding_at_idx",
    }
)
ASSEMBLY = """08002100 <hid_listener.constprop.0>:
 8002100: f000 f87e bl 8002200 <__wrap_zmk_endpoint_send_report>
 8002104: 4770      bx lr
08002300 <zmk_endpoint_clear_reports>:
 8002300: f000 f87e bl 8002400 <__wrap_zmk_hid_keyboard_clear>
 8002304: f000 e8fc blx 8002500 <__wrap_zmk_hid_consumer_clear>
 8002308: 4770      bx lr
08002600 <zmk_usb_hid_init>:
 8002600: f000 f87e bl 8002700 <__wrap_usb_hid_register_device>
 8002604: 4770      bx lr
"""


class RadioImageTests(unittest.TestCase):
    def test_false_assembly_references_cannot_prove_wrapped_hid_calls(self) -> None:
        call = "f000 f87e bl 8002200 <__wrap_zmk_endpoint_send_report>"
        for replacement in (
            "f000 f87e bl 8002200 <zmk_endpoint_send_report>",
            "f000 b87e b.w 8002200 <__wrap_zmk_endpoint_send_report>",
            "4800 ldr r0, [pc] ; bl 8002200 <__wrap_zmk_endpoint_send_report>",
            "4770 bx lr ; bl 8002200 <__wrap_zmk_endpoint_send_report>",
        ):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                check_image.validate_radio_linking(
                    set(RADIO_SYMBOLS),
                    ASSEMBLY.replace(call, replacement),
                    "wired-studio-updater",
                )

    def test_real_bl_and_blx_inside_optimized_listener_are_accepted(self) -> None:
        check_image.validate_radio_linking(
            set(RADIO_SYMBOLS), ASSEMBLY, "wired-studio-updater"
        )
        check_image.validate_radio_linking(
            set(RADIO_SYMBOLS),
            ASSEMBLY.replace("hid_listener.constprop.0", "hid_listener.isra.2").replace(
                "f000 f87e bl 8002200", "f000 e87e blx 8002200"
            ),
            "wired-studio-updater",
        )

    def test_wrapper_symbols_alone_or_calls_in_other_functions_are_insufficient(
        self,
    ) -> None:
        for assembly in ("", ASSEMBLY.replace("hid_listener.constprop.0", "other")):
            with self.subTest(assembly=assembly), self.assertRaises(ValueError):
                check_image.validate_radio_linking(
                    set(RADIO_SYMBOLS), assembly, "wired-studio-updater"
                )
        for name in RADIO_SYMBOLS - {"gmk_radio_poll"}:
            with self.subTest(name=name), self.assertRaises(ValueError):
                check_image.validate_radio_linking(
                    set(RADIO_SYMBOLS) - {name}, ASSEMBLY, "wired-studio-updater"
                )

    def test_clear_wrappers_must_both_be_called_in_the_real_clear_body(self) -> None:
        for wrapper in ("keyboard", "consumer"):
            with self.subTest(wrapper=wrapper), self.assertRaises(ValueError):
                check_image.validate_radio_linking(
                    set(RADIO_SYMBOLS),
                    ASSEMBLY.replace(
                        f"<__wrap_zmk_hid_{wrapper}_clear>",
                        f"<zmk_hid_{wrapper}_clear>",
                    ),
                    "wired-studio-updater",
                )
        with self.assertRaises(ValueError):
            check_image.validate_radio_linking(
                set(RADIO_SYMBOLS),
                ASSEMBLY.replace("<zmk_endpoint_clear_reports>:", "<other_clear>:"),
                "wired-studio-updater",
            )

    def test_marker_requires_exact_explicit_profile_even_without_radio_poll(
        self,
    ) -> None:
        marker = "gmk_radio_test_marker"
        for variant in (
            None,
            "wired-updater",
            "wired-qualification",
            "wired-studio-updater",
            "wired-studio-radio-qualification-test",
        ):
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                check_image.validate_radio_linking({marker}, "", variant)
        with self.assertRaises(ValueError):
            check_image.validate_radio_linking(
                set(RADIO_SYMBOLS), ASSEMBLY, "wired-studio-radio-qualification"
            )
        check_image.validate_radio_linking(
            set(RADIO_SYMBOLS) | {marker},
            ASSEMBLY,
            "wired-studio-radio-qualification",
        )

    def test_existing_nonradio_images_do_not_require_new_wrappers(self) -> None:
        check_image.validate_radio_linking({"main"}, "", "wired-studio-updater")
        check_image.validate_radio_linking({"main"}, "", "wired-qualification")

    def test_radio_images_cannot_replace_usb_readiness_or_endpoint_identity(
        self,
    ) -> None:
        for fake in (
            "__wrap_zmk_usb_is_hid_ready",
            "__wrap_zmk_endpoint_get_selected",
        ):
            with self.subTest(fake=fake), self.assertRaises(ValueError):
                check_image.validate_radio_linking(
                    set(RADIO_SYMBOLS) | {fake}, ASSEMBLY, "wired-studio-updater"
                )

    @staticmethod
    def image_fixture(size: int) -> tuple[bytes, bytes, str]:
        base = 0x08002000
        image = (
            struct.pack("<16I", 0x20006FC0, *([0x08002201] * 15))
            + bytes(size - 80)
            + b"GMK70ZMKBOOT\x01\0\0\0"
        )
        elf = bytearray(0x100 + size)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", elf, 18, 40)
        struct.pack_into("<II", elf, 24, 0x08002201, 52)
        struct.pack_into("<HH", elf, 42, 32, 1)
        struct.pack_into("<8I", elf, 52, 1, 0x100, base, base, size, size, 5, 4)
        elf[0x100:] = image
        required = {
            "main",
            "zmk_keymap_position_state_changed",
            "zmk_usb_get_conn_state",
            "wb32_usb_clock_enable",
            "soc_early_init_hook",
            "gmk_flash_page",
            "gmk_running_size",
            "gmk_zmk_receiver_step",
            "gmk_zmk_raw_hid_init",
            "gmk_wired_scan",
            "gmk_wired_update",
            "_k_thread_data_gmk_scan_thread",
            "_k_thread_data_gmk_serial_thread",
            "_k_thread_data_update_worker",
            "gmk_trial_packet_valid",
            "settings_backend_init",
            "gmk_zmk_settings_poll",
            "gmk_zmk_settings_flash_page",
            "__wrap_usb_hid_register_device",
            "__wrap_zmk_studio_core_get_lock_state",
            "uart_rpc_interface_init",
            "gmk_radio_test_marker",
        } | set(RADIO_SYMBOLS)
        symbols: dict[str, int] = {name: 0x08002200 for name in required}
        symbols.update(
            {
                "__rom_region_start": base,
                "__gmk_image_end": base + size,
                "_flash_used": size,
                "_image_ram_end": 0x20006F00,
            }
        )
        nm = "\n".join(f"{address:08x} T {name}" for name, address in symbols.items())
        return bytes(elf), image, nm

    def test_radio_qualification_uses_permanent_slot_and_studio_settings_limit(
        self,
    ) -> None:
        for size in (0x1A000, 0x1A004):
            elf, image, nm = self.image_fixture(size)

            def output(command: list[str], *, text: bool) -> str:
                self.assertTrue(text)
                if command[:2] == ["arm-none-eabi-nm", "--defined-only"]:
                    return nm
                self.assertEqual(command[:2], ["arm-none-eabi-objdump", "-d"])
                return ASSEMBLY

            with tempfile.TemporaryDirectory() as folder:
                elf_path, image_path = (
                    Path(folder) / "app.elf",
                    Path(folder) / "app.bin",
                )
                elf_path.write_bytes(elf)
                image_path.write_bytes(image)
                with patch("check_image.subprocess.check_output", side_effect=output):
                    if size == 0x1A000:
                        result = check_image.check_wired(
                            elf_path, image_path, "wired-studio-radio-qualification"
                        )
                        self.assertEqual(result["load_address"], 0x08002000)
                        self.assertEqual(
                            result["variant"], "wired-studio-radio-qualification"
                        )
                    else:
                        with self.assertRaises(ValueError):
                            check_image.check_wired(
                                elf_path, image_path, "wired-studio-radio-qualification"
                            )

    def test_image_check_rejects_actual_bypass_and_marker_on_nonradio_image(
        self,
    ) -> None:
        elf, image, nm = self.image_fixture(1024)
        for variant, assembly, symbol_text in (
            (
                "wired-studio-radio-qualification",
                ASSEMBLY.replace(
                    "<__wrap_zmk_endpoint_send_report>", "<zmk_endpoint_send_report>"
                ),
                nm,
            ),
            (
                "wired-studio-updater",
                ASSEMBLY,
                "\n".join(
                    line
                    for line in nm.splitlines()
                    if not line.endswith(" gmk_radio_poll")
                ),
            ),
        ):

            def output(command: list[str], *, text: bool) -> str:
                self.assertTrue(text)
                return symbol_text if command[0] == "arm-none-eabi-nm" else assembly

            with tempfile.TemporaryDirectory() as folder:
                elf_path, image_path = (
                    Path(folder) / "app.elf",
                    Path(folder) / "app.bin",
                )
                elf_path.write_bytes(elf)
                image_path.write_bytes(image)
                with (
                    self.subTest(variant=variant),
                    patch("check_image.subprocess.check_output", side_effect=output),
                    self.assertRaises(ValueError),
                ):
                    check_image.check_wired(elf_path, image_path, variant)


if __name__ == "__main__":
    unittest.main()
