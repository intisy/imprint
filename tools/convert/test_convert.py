import json
import os
import tempfile
import tomllib
import unittest
from pathlib import Path

import convert
import toml_writer


class TomlWriterTest(unittest.TestCase):
    def test_round_trips_through_tomllib(self):
        sample = {
            "extends": "base",
            "persistence_size_mb": 32768,
            "expire_password": True,
            "notes": ['quote " and backslash \\', "line\nbreak", "tab\there", "Umlaut äöü"],
            "empty": [],
            "apps": [f"app-{index}" for index in range(30)],
            "config_files": {".config/i3/config": "config"},
            "disk": {"esp_size_mb": 1024, "data_partition": {"label": "data"}},
            "host_files": [
                {"source": "a", "exclude": ["tests"]},
                {"source": "b", "nested": {"depth": 1}},
            ],
            "empty_table": {},
        }
        self.assertEqual(tomllib.loads(toml_writer.dumps(sample)), sample)

    def test_a_boolean_is_not_written_as_an_integer(self):
        self.assertIn("flag = true", toml_writer.dumps({"flag": True}))

    def test_refuses_a_missing_value(self):
        with self.assertRaises(ValueError):
            toml_writer.dumps({"persistence_size_mb": None})

    def test_refuses_a_list_mixing_tables_and_values(self):
        with self.assertRaises(ValueError):
            toml_writer.dumps({"mixed": [{"a": 1}, "b"]})


class IdentifierTest(unittest.TestCase):
    def test_underscores_become_hyphens(self):
        self.assertEqual(convert.new_id("jetbrains_toolbox"), "jetbrains-toolbox")

    def test_package_named_ids_are_renamed_after_the_project(self):
        self.assertEqual(convert.new_id("vscode"), "visual-studio-code")

    def test_valid_ids_stay(self):
        self.assertEqual(convert.new_id("gpu-nvidia"), "gpu-nvidia")

    def test_an_id_that_cannot_be_made_valid_is_refused(self):
        with self.assertRaises(ValueError):
            convert.new_id("Bad Name")


class MergeTest(unittest.TestCase):
    def test_lists_are_the_union_base_first_without_duplicates(self):
        merged = convert.merge_nodes({"apps": ["git", "base", "git"]}, {"apps": ["steam", "base"]})
        self.assertEqual(merged, {"apps": ["git", "base", "steam"]})

    def test_tables_merge_and_the_child_wins_on_values(self):
        merged = convert.merge_nodes(
            {"install": {"timezone": "UTC", "locale": "en_US.UTF-8"}, "display_manager": "gdm"},
            {"install": {"timezone": "Europe/Berlin"}, "display_manager": "lightdm"},
        )
        self.assertEqual(
            merged,
            {"install": {"timezone": "Europe/Berlin", "locale": "en_US.UTF-8"}, "display_manager": "lightdm"},
        )

    def test_a_null_child_keeps_the_base(self):
        self.assertEqual(convert.merge_nodes({"a": 1}, {"a": None}), {"a": 1})


class ConvertTest(unittest.TestCase):
    def test_a_window_manager_key_becomes_id_and_its_apps_are_converted(self):
        converted = convert.convert_window_manager({
            "key": "i3", "name": "i3 Window Manager", "type": "x11", "session": "xinit",
            "required_apps": ["i3"], "dotfiles_dir": "i3", "config_dirs": {".xinitrc": "xinitrc"},
        })
        self.assertEqual(converted, {
            "id": "i3", "name": "i3", "type": "x11", "session": "xinit", "required_apps": ["i3"],
            "dotfiles_directory": "i3", "config_files": {".xinitrc": "xinitrc"},
        })

    def test_a_distro_family_becomes_its_id(self):
        converted = convert.convert_distro({"family": "arch", "display_name": "Arch / EndeavourOS", "package_manager": "pacman"})
        self.assertEqual(converted, {"id": "arch", "name": "Arch / EndeavourOS", "package_manager": "pacman"})

    def test_bootloader_fields_lose_their_abbreviations(self):
        converted = convert.convert_bootloader({"key": "grub", "name": "GRUB", "install_cmd": "grub-install", "mkconfig_cmd": "grub-mkconfig"})
        self.assertEqual(converted, {"id": "grub", "name": "GRUB", "install_command": "grub-install", "mkconfig_command": "grub-mkconfig"})

    def test_an_app_gets_its_id_and_upstream_name(self):
        converted = convert.convert_app("visual-studio-code", {"name": "VS Code", "category": "development", "install": {"arch": {"aur": ["visual-studio-code-bin"]}}})
        self.assertEqual(converted, {"id": "visual-studio-code", "name": "Visual Studio Code", "category": "development", "install": {"arch": {"aur": ["visual-studio-code-bin"]}}})

    def test_a_setup_is_renamed_and_gets_its_remote_apps(self):
        profile = {
            "schema_version": 1, "name": "workstation", "uid": "d7b22b8c", "extends": "dev-workstation",
            "wms": ["i3"], "apps": ["jetbrains_toolbox"],
            "remove": {"wms": ["gnome"], "apps": ["vscode"]},
            "remote": {"ssh_authorized_keys": ["ssh-ed25519 AAAA test"], "tailscale": True},
        }
        self.assertEqual(convert.convert_setup(profile, extends="base"), {
            "extends": "base",
            "window_managers": ["i3"],
            "apps": ["jetbrains-toolbox", "ssh", "tailscale"],
            "remove": {"window_managers": ["gnome"], "apps": ["visual-studio-code"]},
            "remote": {"ssh_authorized_keys": ["ssh-ed25519 AAAA test"], "tailscale": True},
        })

    def test_no_remote_adds_neither_ssh_nor_tailscale(self):
        self.assertEqual(convert.convert_setup({"apps": ["git"]})["apps"], ["git"])

    def test_empty_ssh_keys_add_no_ssh(self):
        setup = convert.convert_setup({"apps": ["git"], "remote": {"ssh_authorized_keys": []}})
        self.assertEqual(setup["apps"], ["git"])

    def test_tailscale_false_adds_no_tailscale(self):
        setup = convert.convert_setup({"apps": ["git"], "remote": {"tailscale": False}})
        self.assertEqual(setup["apps"], ["git"])

    def test_keys_and_tailscale_add_both_exactly_once(self):
        remote = {"ssh_authorized_keys": ["ssh-ed25519 AAAA test"], "tailscale": True}
        setup = convert.convert_setup({"apps": ["git", "ssh"], "remote": remote})
        self.assertEqual(setup["apps"], ["git", "ssh", "tailscale"])

    def test_a_host_file_from_compose_docs_names_its_repository(self):
        setup = convert.convert_setup({"host_files": [{
            "source": "F:/Documents/GitHub/compose/docs/tools/drive-sync", "target": "~/.local/share/drive-sync"}]})
        self.assertEqual(setup["host_files"], [{
            "repository": "intisy-compose/docs", "source": "tools/drive-sync", "target": "~/.local/share/drive-sync"}])

    def test_a_host_file_on_another_absolute_path_is_refused(self):
        with self.assertRaises(ValueError):
            convert.convert_setup({"host_files": [{"source": "C:/Users/finn/secret", "target": "~/x"}]})

    def test_two_ids_that_convert_to_one_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            apps = Path(directory) / "apps"
            apps.mkdir()
            app = {"name": "Claude Code", "category": "ai", "install": {"arch": {"packages": ["x"]}}}
            (apps / "a.json").write_text(json.dumps({"claude_code": app}), encoding="utf-8")
            (apps / "b.json").write_text(json.dumps({"claude-code": app}), encoding="utf-8")
            with self.assertRaises(ValueError):
                convert.convert_apps(Path(directory))


@unittest.skipUnless(os.environ.get("IMPRINT_VENTOY_DEPLOY"), "set IMPRINT_VENTOY_DEPLOY to the ventoy-deploy checkout")
class RealTreeTest(unittest.TestCase):
    def test_converts_the_whole_tree(self):
        conversion = convert.build(Path(os.environ["IMPRINT_VENTOY_DEPLOY"]))
        self.assertEqual(len(conversion.apps), 81)
        for identifier in conversion.apps:
            self.assertRegex(identifier, convert.IDENTIFIER_PATTERN)
        self.assertEqual(set(conversion.setups), {"base", "workstation", "storage-worker"})
        self.assertNotIn("extends", conversion.setups["base"])
        self.assertEqual(conversion.setups["workstation"]["extends"], "base")
        self.assertIn("jetbrains-toolbox", conversion.setups["workstation"]["apps"])
        self.assertIn("visual-studio-code", conversion.setups["base"]["apps"])
        for name in ("workstation", "storage-worker"):
            for remote_app in ("ssh", "tailscale"):
                self.assertIn(remote_app, conversion.setups[name]["apps"])
        for remote_app in ("ssh", "tailscale"):
            self.assertNotIn(remote_app, conversion.setups["base"]["apps"])
        for setup in list(conversion.setups.values()) + list(conversion.template_setups.values()):
            tomllib.loads(toml_writer.dumps(setup))


if __name__ == "__main__":
    unittest.main()
