import tempfile
import tomllib
import unittest
from pathlib import Path

import freeze_parity

PERSONAL_SETUP = """
extends = "base"
distro = "arch"
apps = ["git"]
notes = ["personal note"]

[install]
username = "someone"

[remote]
ssh_authorized_keys = ["ssh-ed25519 AAAA key"]

[[users]]
name = "someone"

[extra_packages]
arch = ["rsync"]
"""


class FreezeParityTest(unittest.TestCase):
    def test_keeps_only_the_keys_resolution_reads(self):
        stripped = freeze_parity.strip_setup(tomllib.loads(PERSONAL_SETUP))
        self.assertEqual(
            stripped,
            {"extends": "base", "distro": "arch", "apps": ["git"], "extra_packages": {"arch": ["rsync"]}},
        )

    def test_writes_every_frozen_setup_without_personal_keys(self):
        with tempfile.TemporaryDirectory() as workspace:
            data = Path(workspace) / "data"
            (data / "setups").mkdir(parents=True)
            for name in freeze_parity.SETUPS:
                (data / "setups" / f"{name}.toml").write_text(PERSONAL_SETUP, encoding="utf-8")
            output = Path(workspace) / "output"
            freeze_parity.freeze_setups(data, output)
            for name in freeze_parity.SETUPS:
                text = (output / "data" / "setups" / f"{name}.toml").read_text(encoding="utf-8")
                self.assertNotIn("someone", text)
                self.assertNotIn("ssh-ed25519", text)
                self.assertNotIn("\r", text)


if __name__ == "__main__":
    unittest.main()
