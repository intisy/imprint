#!/bin/sh
# Written by imprint for the setup 'hooked': runs its firstboot hooks once, and all of them again on the next boot after a failure.
set -u
bundle=/var/lib/imprint/firstboot
log=/var/log/imprint/firstboot.log
unit=imprint-firstboot.service
export IMPRINT_SETUP="$bundle/setup.json"
export IMPRINT_PERSONAL="$bundle/personal"
export IMPRINT_DATA="$bundle/data"
export IMPRINT_STAGE=firstboot
mkdir -p /var/log/imprint

say() {
    echo "imprint: $*" | tee -a "$log"
}

run_hook() {
    say "firstboot hook $1 of $2: $3"
    status_file=$(mktemp)
    { sh "$IMPRINT_DATA/$3"; echo "$?" >"$status_file"; } 2>&1 | tee -a "$log"
    status=$(cat "$status_file")
    rm -f "$status_file"
    if [ "$status" -ne 0 ]; then
        say "$3 failed with exit code $status; the firstboot hooks run again on the next boot"
        exit "$status"
    fi
}

run_hook 1 2 'hooks/first.sh'
run_hook 2 2 'hooks/second.sh'

say "every firstboot hook of setup 'hooked' succeeded; removing $unit"
rm -f "/etc/systemd/system/multi-user.target.wants/$unit" "/etc/systemd/system/$unit"
rm -rf "$bundle"
systemctl daemon-reload 2>/dev/null || true
