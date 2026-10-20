#!/bin/sh
# Bundles a setup and runs its install.sh against stand-ins for the live system's disk tools.
# usage: stick-install.sh MANAGER FIXTURES WORK
set -eu

manager=$1
fixtures=$2
work=$3

rm -rf "$work"
mkdir -p "$work/bin" "$work/stick" "$work/mounted"
calls="$work/calls"
: >"$calls"

stand_in() {
    printf '#!/bin/sh\necho "%s $*" >>"%s"\n%s\n' "$1" "$calls" "$2" >"$work/bin/$1"
    chmod 755 "$work/bin/$1"
}

stand_in id 'echo 0'
stand_in mountpoint 'exit 1'
stand_in blkid 'echo /dev/sdb1'
stand_in blockdev 'echo 62521344'
stand_in dmsetup ''
stand_in mount "cp -R '$work/stick/.' '$work/mounted/'"
stand_in findmnt 'case "$*" in *SOURCE*) echo /dev/loop0 ;; *) echo /srv ;; esac'
stand_in umount ''
printf '#!/bin/sh\necho "engine $*" >>"%s"\n' "$calls" >"$work/fake-engine"

"$manager" --root "$fixtures" bundle workstation "$work/stick/imprint" --engine "$work/fake-engine" >/dev/null

PATH="$work/bin:$PATH" IMPRINT_STICK_MOUNT="$work/mounted" sh "$work/stick/imprint/install.sh" --disk /dev/nvme0n1 \
    2>"$work/said"

expect() {
    if ! grep -qxF -- "$1" "$calls"; then
        echo "FAIL: no call '$1' in:" >&2
        cat "$calls" >&2
        exit 1
    fi
}

expect "dmsetup create imprint-stick --readonly --table 0 62521344 linear /dev/sdb1 0"
expect "mount -o ro /dev/mapper/imprint-stick $work/mounted"
expect "umount -l /srv"
expect "engine install $work/mounted/imprint/workstation.json --disk /dev/nvme0n1"
if [ "$(tail -n 1 "$calls")" != "engine install $work/mounted/imprint/workstation.json --disk /dev/nvme0n1" ]; then
    echo "FAIL: the engine is not the last call" >&2
    exit 1
fi
grep -q "read-only through /dev/mapper/imprint-stick" "$work/said" || { echo "FAIL: no stick line" >&2; exit 1; }
echo "stick install: ok"
