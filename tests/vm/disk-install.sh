#!/bin/bash
# Installs a setup with imprint-engine in QEMU, offline, onto a 1 TB system disk and a 2 TB data disk, then logs in through the display manager and checks the result.
# Run as root on a Linux host with KVM (WSL works). Only the image files named below are ever opened.
set -euo pipefail

usage() {
    cat >&2 <<'TEXT'
usage: disk-install.sh ISO STICK BUNDLE WORK
  ISO     an Arch live ISO (archiso); its kernel boots the VM
  STICK   a stick image carrying the offline repositories (a Ventoy stick in ventoy-deploy's layout); used through a
          qcow2 overlay and never written
  BUNDLE  the directory `imprint bundle <setup> BUNDLE --engine <static imprint-engine>` wrote
  WORK    a directory for the disks, logs and the result
environment: IMPRINT_VM_PASSWORD (default imprint4test), IMPRINT_VM_MEMORY_MB (default 6144),
             IMPRINT_VM_INSTALL_TIMEOUT (seconds, default 14400),
             IMPRINT_VM_REUSE_INSTALL=1 (skip the install, log in and verify the disks already in WORK)
TEXT
    exit 2
}

[ $# -eq 4 ] || usage
iso=$(realpath "$1")
stick=$(realpath "$2")
bundle=$(realpath "$3")
work=$(realpath -m "$4")
here=$(cd "$(dirname "$0")" && pwd)
password=${IMPRINT_VM_PASSWORD:-imprint4test}
memory=${IMPRINT_VM_MEMORY_MB:-6144}
install_timeout=${IMPRINT_VM_INSTALL_TIMEOUT:-14400}
ovmf_code=/usr/share/OVMF/OVMF_CODE_4M.fd
ovmf_variables=/usr/share/OVMF/OVMF_VARS_4M.fd

say() {
    echo "[disk-install] $*"
}

fail() {
    say "FAIL: $*"
    exit 1
}

extract_live_kernel() {
    local mount_point="$work/iso"
    mkdir -p "$mount_point"
    mount -o loop,ro "$iso" "$mount_point"
    cp "$mount_point/arch/boot/x86_64/vmlinuz-linux" "$mount_point/arch/boot/x86_64/initramfs-linux.img" "$work/"
    umount "$mount_point"
    iso_label=$(blkid -s LABEL -o value "$iso")
    [ -n "$iso_label" ] || fail "the ISO has no volume label"
}

setup_name() {
    find "$bundle" -maxdepth 1 -name '*.json' -printf '%f\n' | head -1 | sed 's/\.json$//'
}

write_expected_packages() {
    python3 - "$bundle/$1.json" "$work/testdisk/expected" <<'PYTHON'
import json, os, sys
resolved = json.load(open(sys.argv[1]))
os.makedirs(sys.argv[2], exist_ok=True)
for key in ("packages", "aur", "flatpak", "services", "user_services"):
    with open(os.path.join(sys.argv[2], key), "w", newline="\n") as listing:
        listing.write("".join(item + "\n" for item in resolved[key]))
with open(os.path.join(sys.argv[2], "username"), "w", newline="\n") as name:
    name.write(resolved["setup"].get("install", {}).get("username", "user") + "\n")
PYTHON
}

make_test_disk() {
    local name=$1
    rm -rf "$work/testdisk" "$work/test.img"
    mkdir -p "$work/testdisk"
    cp "$here/guest-install.sh" "$here/guest-verify.sh" "$work/testdisk/"
    cp -r "$bundle" "$work/testdisk/bundle"
    printf '5\n%s\n%s\n1\n' "$password" "$password" > "$work/testdisk/answers.txt"
    echo "$name" > "$work/testdisk/setup-name"
    write_expected_packages "$name"
    sed -i 's/\r$//' "$work/testdisk/"*.sh
    mkfs.ext4 -q -L IMPRINTTEST -d "$work/testdisk" "$work/test.img" 512M
}

make_disks() {
    rm -f "$work/system.qcow2" "$work/data.qcow2" "$work/stick.qcow2"
    qemu-img create -q -f qcow2 "$work/system.qcow2" 1T
    qemu-img create -q -f qcow2 "$work/data.qcow2" 2T
    qemu-img create -q -f qcow2 -F "$(qemu-img info --output=json "$stick" | python3 -c 'import json,sys; print(json.load(sys.stdin)["format"])')" \
        -b "$stick" "$work/stick.qcow2"
}

common_devices=()
set_common_devices() {
    common_devices=(-enable-kvm -machine q35 -cpu host -smp 4 -m "$memory"
        -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code" -drive "if=pflash,format=raw,file=$work/vars.fd"
        -drive "if=none,id=system,file=$work/system.qcow2,format=qcow2" -device nvme,serial=IMPRINTSYSTEM,drive=system
        -drive "if=none,id=data,file=$work/data.qcow2,format=qcow2" -device nvme,serial=IMPRINTDATA,drive=data
        -nic user,model=virtio-net-pci,restrict=on -monitor "unix:$work/monitor.sock,server,nowait" -display none -vga std)
}

run_live() {
    local script=$1 seconds=$2 log=$3
    local append="archisobasedir=arch archisolabel=$iso_label console=ttyS0,115200 \
systemd.run=\"/usr/bin/mount -m -L IMPRINTTEST /run/t\" systemd.run=\"/usr/bin/bash /run/t/$script\" \
systemd.run_success_action=poweroff systemd.run_failure_action=poweroff"
    cp "$ovmf_variables" "$work/vars.fd"
    rm -f "$work/monitor.sock"
    timeout "$seconds" qemu-system-x86_64 "${common_devices[@]}" \
        -kernel "$work/vmlinuz-linux" -initrd "$work/initramfs-linux.img" -append "$append" \
        -drive "if=none,id=cd,media=cdrom,readonly=on,file=$iso" -device ide-cd,drive=cd \
        -device qemu-xhci,id=xhci -drive "if=none,id=stick,file=$work/stick.qcow2,format=qcow2" \
        -device usb-storage,bus=xhci.0,drive=stick,removable=on \
        -drive "if=none,id=test,file=$work/test.img,format=raw" -device virtio-blk-pci,drive=test \
        -serial "file:$log" || fail "the live VM did not finish within $seconds seconds"
}

monitor() {
    python3 "$here/monitor.py" "$work/monitor.sock" "$@"
}

type_text() {
    local text=$1 keys=() character
    for ((index = 0; index < ${#text}; index++)); do
        character=${text:index:1}
        keys+=("sendkey $character")
    done
    monitor "${keys[@]}" "sendkey ret"
}

# A fresh firmware variable store, so the disk must boot through GRUB's removable path alone.
login_through_display_manager() {
    cp "$ovmf_variables" "$work/vars.fd"
    rm -f "$work/monitor.sock"
    qemu-system-x86_64 "${common_devices[@]}" -serial "file:$work/login-serial.log" &
    local machine=$!
    sleep 240
    monitor "screendump $work/greeter.ppm"
    type_text "$password"
    sleep 120
    monitor "screendump $work/session.ppm" "system_powerdown"
    for ((waited = 0; waited < 180; waited += 5)); do
        kill -0 "$machine" 2>/dev/null || break
        sleep 5
    done
    kill "$machine" 2>/dev/null || true
    wait "$machine" 2>/dev/null || true
}

read_result() {
    local name=$1
    mkdir -p "$work/results"
    mount -o loop,ro "$work/test.img" "$work/results"
    cp "$work/results/$name".* "$work/" 2>/dev/null || true
    umount "$work/results"
}

# Only START is answered, so the review's defaults apply: the data partition at threshold 50.
check_storage_choice() {
    python3 - "$work/install.firstboot-setup.json" <<'PYTHON' || fail "choices.storage in the firstboot bundle is not the review's default"
import json, sys
storage = json.load(open(sys.argv[1]))["choices"]["storage"]
print("[disk-install] choices.storage in the firstboot bundle:", json.dumps(storage, sort_keys=True))
sys.exit(0 if (storage["node"], storage.get("path"), storage["threshold"]) == ("data-disk", "/data", 50) else 1)
PYTHON
}

mkdir -p "$work"
extract_live_kernel
name=$(setup_name)
[ -n "$name" ] || fail "no <setup>.json in $bundle"
say "installing $name from $stick, offline"
make_test_disk "$name"
set_common_devices
if [ "${IMPRINT_VM_REUSE_INSTALL:-0}" = 1 ]; then
    say "reusing the installed disks in $work"
else
    make_disks
    run_live guest-install.sh "$install_timeout" "$work/install-serial.log"
    read_result install
    [ "$(cat "$work/install.exit" 2>/dev/null)" = 0 ] || fail "imprint-engine install failed, see $work/install.log"
    check_storage_choice
    say "install passed; logging in through the display manager"
fi
login_through_display_manager
say "verifying the installed system"
run_live guest-verify.sh 1800 "$work/verify-serial.log"
read_result verify
cat "$work/verify.log"
[ "$(cat "$work/verify.exit" 2>/dev/null)" = 0 ] || fail "the installed system failed its checks, see $work/verify.log"
say "PASS: $name installed offline onto two disks, booted through the removable path, and its user logged in at the greeter"
