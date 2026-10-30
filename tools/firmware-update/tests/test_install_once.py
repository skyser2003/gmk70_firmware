"""INSTALL must never be reissued after a missing reply, including STATUS loss."""

import unittest
from unittest.mock import patch

from test_qualified_install import Clock
from test_update import FakeLink, package

from update import Op, Updater


class LostInstallLink(FakeLink):
    def __init__(self, *, status_available: bool) -> None:
        super().__init__()
        self.status_available = status_available
        self.await_install_status = False

    def exchange(self, packet: bytes) -> bytes:
        if packet[4] == Op.INFO:
            self.await_install_status = False
        if (
            packet[4] == Op.STATUS
            and self.await_install_status
            and not self.status_available
        ):
            self.operations.append((packet[3], packet[4]))
            raise OSError("STATUS unavailable after INSTALL")
        reply = super().exchange(packet)
        if packet[4] == Op.INSTALL:
            self.await_install_status = True
            raise OSError("INSTALL reply lost after dispatch")
        return reply


class InstallOnceTests(unittest.TestCase):
    def test_full_install_observes_success_without_replay_or_abort_after_ack_loss(
        self,
    ) -> None:
        for available in (True, False):
            clock = Clock()
            link = LostInstallLink(status_available=available)
            with (
                self.subTest(status_available=available),
                patch("update.time.monotonic", side_effect=clock.monotonic),
                patch("update.time.sleep", side_effect=clock.sleep),
            ):
                Updater(link).install(0, package())
            self.assertEqual(link.operations.count((0, Op.INSTALL)), 1)
            self.assertNotIn((0, Op.ABORT), link.operations)

    def test_low_level_install_command_also_uses_single_dispatch(self) -> None:
        clock = Clock()
        link = LostInstallLink(status_available=True)
        link.sessions[0] = 37
        with (
            patch("update.time.monotonic", side_effect=clock.monotonic),
            patch("update.time.sleep", side_effect=clock.sleep),
        ):
            reply = Updater(link).command(0, Op.INSTALL, 37)
        self.assertEqual(reply.operation, Op.INSTALL)
        self.assertEqual(reply.session, 37)
        self.assertEqual(link.operations.count((0, Op.INSTALL)), 1)


if __name__ == "__main__":
    unittest.main()
