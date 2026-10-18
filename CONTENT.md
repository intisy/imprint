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

Clone with `git clone --recursive`, or run `git submodule update --init` after cloning.
