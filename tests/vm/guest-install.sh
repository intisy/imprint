#!/bin/bash
# Runs in the live VM: puts the bundle on the stick as a person would, then installs from the stick.
set -u
exec > >(tee /run/t/install.log > /dev/ttyS0) 2>&1

finish() {
    echo "$1" > /run/t/install.exit
    sync
    exit 0
}

# The firstboot bundle removes itself once its hooks succeed, so its setup is copied before the first boot.
keep_firstboot_setup() {
    local system_disk
    system_disk=$(lsblk -dnpo NAME,SERIAL | awk '$2 == "IMPRINTSYSTEM" {print $1}')
    mkdir -p /run/installed
    # Not `-o ro`: after a lazy unmount the root can still be mounted read-write, and mount refuses to change that.
    mount "${system_disk}p3" /run/installed || return 0
    cp /run/installed/var/lib/imprint/firstboot/setup.json /run/t/install.firstboot-setup.json
    umount /run/installed
}

udevadm settle
stick_partition=$(blkid -L Ventoy) || { echo "no partition labelled Ventoy"; finish 3; }
mkdir -p /run/stick
mount "$stick_partition" /run/stick || finish 3
rm -rf /run/stick/imprint
mkdir -p /run/stick/imprint
cp -r /run/t/bundle/. /run/stick/imprint/
sync
mount -o remount,ro /run/stick || finish 3
name=$(cat /run/t/setup-name)
lsblk -o NAME,SIZE,TYPE,TRAN,MODEL
/run/stick/imprint/imprint-engine install "/run/stick/imprint/$name.json" --answers /run/t/answers.txt
status=$?
keep_firstboot_setup
finish $status
