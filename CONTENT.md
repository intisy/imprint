## How it fits together

- `catalog/` is `intisy/imprint-catalog`, pinned to one commit: how to install each app on each distro.
- `data/` is your data repo: the setups, your catalog entries, tags and dotfiles. It starts as
  `intisy/imprint-data-template`; `imprint data use owner/repo` points it at yours.
- `imprint` is the manager, for Windows, Linux and macOS. `imprint-engine` runs on the target.

## Commands

    imprint setups                list the setups in data/setups
    imprint setup show <name>     a setup with everything it inherits, resolved against the catalog
    imprint validate              check the catalog and every setup
    imprint data use owner/repo   point data/ at another data repo
    imprint catalog update        move the catalog pin to the newest catalog commit
    imprint hooks run <stage> <name> [--personal PERSONAL]
                                  run the setup's hooks of one stage, in order
    imprint hooks install-firstboot <name> --target ROOT [--personal PERSONAL]
                                  make the installed system at ROOT run the firstboot hooks once

Clone with `git clone --recursive`, or run `git submodule update --init` after cloning.

## Hooks

A setup lists shell scripts from its data repo by stage (`build`, `install`, `firstboot`,
`session`):

    [[hooks]]
    stage  = "firstboot"
    script = "hooks/storage-join.sh"

A setup's hooks run after those of the setup it `extends`, in the order written; the same hook
listed twice runs once. A stage stops at the first script that fails and exits with its code.
Each script runs with `sh` and gets `IMPRINT_SETUP` (the resolved setup as JSON), `IMPRINT_DATA`
(the data repo), `IMPRINT_PERSONAL` (the stick's `personal/` folder) and `IMPRINT_STAGE`.

`firstboot` hooks run as root after `network-online.target`, from `imprint-firstboot.service`
in the installed system: it removes itself once every hook succeeded and runs all of them again
on the next boot after a failure, so a firstboot hook must be safe to run twice. Its log is
`/var/log/imprint/firstboot.log` and `journalctl -u imprint-firstboot`. The engine offers the
same as `imprint-engine hooks run` and `imprint-engine hooks install-firstboot`, working from a
resolved setup instead of the data repo's setups.
