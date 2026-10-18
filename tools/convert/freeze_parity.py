"""Freezes the inputs of the parity test from a data checkout and a catalog checkout.

imprint is public and imprint-data is private, so only the keys that setup resolution reads are kept.
Everything else (users, root, system, console, remote, host_files, app_config, notes, install) is dropped.
"""
import argparse
import shutil
import sys
import tomllib
from pathlib import Path

import toml_writer

SETUPS = ("base", "workstation", "storage-worker")
RESOLUTION_KEYS = (
    "extends",
    "remove",
    "distro",
    "window_managers",
    "apps",
    "bootloader",
    "extra_packages",
    "extra_aur",
    "flatpaks",
    "extra_services",
    "user_services",
)
CATALOG_FOLDERS = ("curated", "generated")


def strip_setup(setup):
    return {key: setup[key] for key in RESOLUTION_KEYS if key in setup}


def freeze_setups(data_directory, output_directory):
    target = Path(output_directory) / "data" / "setups"
    target.mkdir(parents=True, exist_ok=True)
    for name in SETUPS:
        source = Path(data_directory) / "setups" / f"{name}.toml"
        setup = tomllib.loads(source.read_text(encoding="utf-8-sig"))
        (target / f"{name}.toml").write_text(toml_writer.dumps(strip_setup(setup)), encoding="utf-8", newline="\n")


def freeze_catalog(catalog_directory, output_directory):
    for folder in CATALOG_FOLDERS:
        destination = Path(output_directory) / "catalog" / folder
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(Path(catalog_directory) / folder, destination)


def main(arguments):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_directory", help="a checkout of imprint-data")
    parser.add_argument("catalog_directory", help="a checkout of imprint-catalog")
    parser.add_argument("output_directory", help="usually tests/parity/root")
    parsed = parser.parse_args(arguments)
    freeze_setups(parsed.data_directory, parsed.output_directory)
    freeze_catalog(parsed.catalog_directory, parsed.output_directory)


if __name__ == "__main__":
    main(sys.argv[1:])
