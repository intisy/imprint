"""Turns ventoy-deploy's catalog and setups into the first imprint-catalog and imprint-data content.

It runs once. The generated catalog of plan 3 replaces its app recipes afterwards.
"""
import argparse
import copy
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import toml_writer

IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")

ID_RENAMES = {
    "vscode": "visual-studio-code",
    "rdp": "remmina",
}

DISPLAY_NAMES = {
    "visual-studio-code": "Visual Studio Code",
    "remmina": "Remmina",
    "i3": "i3",
    "zed": "Zed",
    "kitty": "kitty",
    "podman": "Podman",
    "thunar": "Thunar",
    "pcmanfm": "PCManFM",
    "hyprland": "Hyprland",
    "sway": "Sway",
    "kde": "KDE Plasma",
    "kde-plasma": "KDE Plasma",
    "gnome": "GNOME",
    "tailscale": "Tailscale",
    "ollama": "Ollama",
    "aider": "Aider",
    "btop": "btop",
    "yazi": "Yazi",
    "lazygit": "lazygit",
    "iwd": "iwd",
    "timeshift": "Timeshift",
    "starship": "Starship",
}

FIELD_RENAMES = {
    "config_dirs": "config_files",
    "dotfiles_dir": "dotfiles_directory",
    "install_cmd": "install_command",
    "mkconfig_cmd": "mkconfig_command",
    "resume_cmdline": "resume_command_line",
}

PROFILE_FIELD_RENAMES = {"distro_family": "distro", "wms": "window_managers"}
DROPPED_PROFILE_FIELDS = {"schema_version", "name", "uid", "created", "extends"}
COMPOSE_DOCS_PREFIX = "F:/Documents/GitHub/compose/docs/"
COMPOSE_DOCS_REPOSITORY = "intisy-compose/docs"

EXAMPLE_SETUP = {
    "distro": "arch",
    "window_managers": ["i3"],
    "apps": ["base", "networkmanager", "pipewire", "git", "firefox", "kitty"],
    "notes": ["example: Arch with i3 and a few apps. Copy it, name it after what the system is for, make it yours."],
    "install": {"timezone": "UTC", "locale": "en_US.UTF-8"},
}


@dataclass
class Conversion:
    apps: dict
    distros: list
    window_managers: list
    bootloaders: list
    setups: dict
    template_setups: dict


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def new_id(old_id):
    converted = ID_RENAMES.get(old_id, old_id.replace("_", "-"))
    if not IDENTIFIER_PATTERN.match(converted):
        raise ValueError(f"'{old_id}' converts to '{converted}', which is not a valid id")
    return converted


def rename_fields(table):
    return {FIELD_RENAMES.get(key, key): value for key, value in table.items()}


def with_id(identifier, name, table, skipped_keys):
    converted = {"id": identifier, "name": DISPLAY_NAMES.get(identifier, name)}
    for key, value in rename_fields(table).items():
        if key not in skipped_keys:
            converted[key] = value
    return converted


def convert_app(identifier, app):
    return with_id(identifier, app["name"], app, {"name"})


def convert_apps(catalog_directory):
    apps = {}
    origins = {}
    for path in sorted((Path(catalog_directory) / "apps").glob("*.json")):
        if path.name == "_custom.json":
            raise ValueError(f"{path} is a local overlay; move its entries into data/catalog by hand")
        for old_id, app in read_json(path).items():
            identifier = new_id(old_id)
            if identifier in apps:
                raise ValueError(f"'{old_id}' in {path.name} and {origins[identifier]} both convert to '{identifier}'")
            origins[identifier] = f"'{old_id}' in {path.name}"
            apps[identifier] = convert_app(identifier, app)
    return apps


def convert_distro(distro):
    converted = {"id": distro["family"], "name": distro["display_name"]}
    for key, value in distro.items():
        if key not in ("family", "display_name"):
            converted[key] = value
    return converted


def convert_window_manager(window_manager):
    identifier = new_id(window_manager["key"])
    converted = with_id(identifier, window_manager["name"], window_manager, {"key", "name"})
    converted["required_apps"] = [new_id(app) for app in converted.get("required_apps", [])]
    return converted


def convert_bootloader(bootloader):
    return with_id(new_id(bootloader["key"]), bootloader["name"], bootloader, {"key", "name"})


def as_list(value):
    return value if isinstance(value, list) else [value]


def union(base, child):
    merged = []
    seen = set()
    for item in base + child:
        marker = json.dumps(item, sort_keys=True)
        if marker not in seen:
            seen.add(marker)
            merged.append(copy.deepcopy(item))
    return merged


def merge_nodes(base, child):
    """Merge-VdNode of ventoy-deploy's manager/ProfileStore.psm1, which imprint must match."""
    if child is None:
        return copy.deepcopy(base)
    if base is None:
        return copy.deepcopy(child)
    if isinstance(base, list) or isinstance(child, list):
        return union(as_list(base), as_list(child))
    if isinstance(base, dict) and isinstance(child, dict):
        keys = list(base) + [key for key in child if key not in base]
        return {key: merge_nodes(base.get(key), child.get(key)) for key in keys}
    return copy.deepcopy(child)


def remote_access_apps(profile):
    """Get-RemoteAccessApps of ventoy-deploy's manager/Resolve.psm1, written into the setup instead of resolved."""
    remote = profile.get("remote") or {}
    apps = []
    if remote.get("ssh_authorized_keys"):
        apps.append("ssh")
    if remote.get("tailscale"):
        apps.append("tailscale")
    return apps


def convert_host_file(entry):
    source = entry["source"].replace("\\", "/")
    if source.startswith(COMPOSE_DOCS_PREFIX):
        converted = {"repository": COMPOSE_DOCS_REPOSITORY, "source": source[len(COMPOSE_DOCS_PREFIX):]}
        converted.update({key: value for key, value in entry.items() if key != "source"})
        return converted
    if source.startswith("/") or re.match(r"^[A-Za-z]:/", source):
        raise ValueError(f"host file source '{source}' is a path on one machine; give it a repository")
    return dict(entry)


def convert_remove(remove):
    converted = {}
    for key, value in remove.items():
        key = PROFILE_FIELD_RENAMES.get(key, key)
        converted[key] = [new_id(app) for app in value] if key == "apps" else value
    return converted


def convert_system(system):
    converted = copy.deepcopy(system)
    policy = converted.get("sudo_policy")
    if policy is not None and "timeout_secs" in policy:
        policy["timeout_seconds"] = policy.pop("timeout_secs")
    return converted


def convert_profile_value(key, value):
    if key == "apps":
        return [new_id(app) for app in value]
    if key == "remove":
        return convert_remove(value)
    if key == "app_config":
        return {new_id(app): config for app, config in value.items()}
    if key == "host_files":
        return [convert_host_file(entry) for entry in value]
    if key == "system":
        return convert_system(value)
    return copy.deepcopy(value)


def convert_setup(profile, extends=None, remote_source=None):
    if "distro" in profile and "distro_family" in profile:
        raise ValueError("a profile with both distro and distro_family needs converting by hand")
    setup = {"extends": extends} if extends else {}
    for key, value in profile.items():
        if key in DROPPED_PROFILE_FIELDS:
            continue
        renamed = PROFILE_FIELD_RENAMES.get(key, key)
        setup[renamed] = convert_profile_value(renamed, value)
    added = [app for app in remote_access_apps(remote_source or profile) if app not in setup.get("apps", [])]
    if added:
        setup["apps"] = setup.get("apps", []) + added
    return setup


def ids_named(setup, key):
    named = list(setup.get(key, []))
    named += setup.get("remove", {}).get(key, [])
    if key == "apps":
        named += list(setup.get("app_config", {}))
    return named


def check_references(conversion):
    window_manager_ids = {window_manager["id"] for window_manager in conversion.window_managers}
    problems = []
    for window_manager in conversion.window_managers:
        problems += [f"window manager {window_manager['id']}: unknown app {app}"
                     for app in window_manager["required_apps"] if app not in conversion.apps]
    every_setup = {**conversion.setups, **conversion.template_setups}
    for name, setup in every_setup.items():
        problems += [f"setup {name}: unknown app {app}" for app in ids_named(setup, "apps") if app not in conversion.apps]
        problems += [f"setup {name}: unknown window manager {window_manager}"
                     for window_manager in ids_named(setup, "window_managers") if window_manager not in window_manager_ids]
    if problems:
        raise ValueError("\n".join(problems))


def flatten_base(base_allgpu, dev_workstation):
    if dev_workstation.get("extends") != "base-allgpu":
        raise ValueError("dev-workstation no longer extends base-allgpu; check the conversion by hand")
    if "remove" in base_allgpu or "remove" in dev_workstation:
        raise ValueError("a remove in base-allgpu or dev-workstation would apply later in ventoy-deploy than here")
    return merge_nodes(base_allgpu, dev_workstation)


def build(source):
    source = Path(source)
    catalog = source / "catalog"
    base_profile = flatten_base(read_json(catalog / "profiles" / "base-allgpu.json"),
                                read_json(catalog / "profiles" / "dev-workstation.json"))
    workstation = read_json(source / "profiles" / "workstation.json")
    if workstation.get("extends") != "dev-workstation":
        raise ValueError("workstation no longer extends dev-workstation; check the conversion by hand")
    conversion = Conversion(
        apps=convert_apps(catalog),
        distros=[convert_distro(read_json(path)) for path in sorted((catalog / "distros").glob("*.json"))],
        window_managers=[convert_window_manager(read_json(path)) for path in sorted((catalog / "wm").glob("*.json"))],
        bootloaders=[convert_bootloader(read_json(path)) for path in sorted((catalog / "bootloaders").glob("*.json"))],
        setups={
            "base": convert_setup(base_profile),
            "workstation": convert_setup(workstation, extends="base", remote_source=merge_nodes(base_profile, workstation)),
            "storage-worker": convert_setup(read_json(source / "profiles" / "storage-migrate.json")),
        },
        template_setups={"example": copy.deepcopy(EXAMPLE_SETUP)},
    )
    check_references(conversion)
    return conversion


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def write_outputs(conversion, catalog_out, data_out, template_out):
    catalog_out, data_out, template_out = Path(catalog_out), Path(data_out), Path(template_out)
    for identifier, app in conversion.apps.items():
        write_text(catalog_out / "generated" / "apps" / f"{identifier}.json", json.dumps(app, indent=2, ensure_ascii=False) + "\n")
    curated = [("distros", conversion.distros), ("window-managers", conversion.window_managers),
               ("bootloaders", conversion.bootloaders)]
    for folder, entries in curated:
        for entry in entries:
            write_text(catalog_out / "curated" / folder / f"{entry['id']}.toml", toml_writer.dumps(entry))
    for directory, setups in ((data_out, conversion.setups), (template_out, conversion.template_setups)):
        for name, setup in setups.items():
            write_text(directory / "setups" / f"{name}.toml", toml_writer.dumps(setup))
        for folder in ("catalog", "tags"):
            write_text(directory / folder / ".gitkeep", "")


def main(arguments=None):
    parser = argparse.ArgumentParser(description="Convert ventoy-deploy's catalog and setups for imprint, once.")
    parser.add_argument("--source", required=True, type=Path, help="the ventoy-deploy checkout")
    parser.add_argument("--catalog-out", required=True, type=Path, help="the imprint-catalog checkout")
    parser.add_argument("--data-out", required=True, type=Path, help="the imprint-data checkout")
    parser.add_argument("--template-out", required=True, type=Path, help="the imprint-data-template checkout")
    options = parser.parse_args(arguments)
    conversion = build(options.source)
    write_outputs(conversion, options.catalog_out, options.data_out, options.template_out)
    print(f"{len(conversion.apps)} apps, {len(conversion.distros)} distros, "
          f"{len(conversion.window_managers)} window managers, {len(conversion.bootloaders)} bootloaders; "
          f"setups {', '.join(conversion.setups)}; template {', '.join(conversion.template_setups)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
