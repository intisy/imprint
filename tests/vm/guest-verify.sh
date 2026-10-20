#!/bin/bash
# Runs in the live VM after the login boot: mounts the installed disk and checks it, after ventoy-deploy's verify.sh.
set -u
exec > >(tee /run/t/verify.log > /dev/ttyS0) 2>&1

root=/mnt
expected=/run/t/expected
user=$(cat "$expected/username")
failures=0

check() {
    local description=$1
    shift
    if "$@" >/dev/null 2>&1; then
        echo "ok   $description"
    else
        echo "FAIL $description"
        failures=$((failures + 1))
    fi
}

absent() {
    ! "$@"
}

package_installed() {
    pacman --root "$root" --dbpath "$root/var/lib/pacman" -Q "$1"
}

unit_enabled() {
    find "$root/etc/systemd/system" -name "$1" -o -name "$1.service" | grep -q .
}

journal_has() {
    journalctl -D "$root/var/log/journal" --no-pager -o cat | grep -E -q "$1"
}

disk_with_serial() {
    lsblk -dnpo NAME,SERIAL | awk -v serial="$1" '$2 == serial {print $1}'
}

fstab_mounts() {
    awk -v point="$1" '$1 !~ /^#/ && $2 == point {found = 1} END {exit !found}' "$root/etc/fstab"
}

owned_by_user() {
    test "$(stat -c %u "$1")" = "$(awk -F: -v name="$user" '$1 == name {print $3}' "$root/etc/passwd")"
}

partition_labels() {
    lsblk -nro PARTLABEL "$1" | sed '/^$/d' | tr '\n' ' '
}

udevadm settle
system_disk=$(disk_with_serial IMPRINTSYSTEM)
data_disk=$(disk_with_serial IMPRINTDATA)
mount "${system_disk}p3" "$root"
mount "${system_disk}p1" "$root/boot"
mount "${data_disk}p1" "$root/data"

check "system disk: ESP, swap, root" test "$(partition_labels "$system_disk")" = "ESP swap root "
check "data disk: one partition labelled data" test "$(partition_labels "$data_disk")" = "data "
for point in / /boot /data none; do check "fstab mounts $point" fstab_mounts "$point"; done
check "/data belongs to $user" owned_by_user "$root/data"
while read -r package; do check "package $package" package_installed "$package"; done < "$expected/packages"
while read -r package; do check "AUR package $package" package_installed "$package"; done < "$expected/aur"
for package in tailscale syncthing curl jq; do check "storage join needs $package" package_installed "$package"; done
while read -r app; do check "Flatpak $app" test -d "$root/var/lib/flatpak/app/$app"; done < "$expected/flatpak"
while read -r service; do check "service $service enabled" unit_enabled "$service"; done < "$expected/services"
check "a display manager is enabled" test -L "$root/etc/systemd/system/display-manager.service"
check "the i3 config is in the home" test -f "$root/home/$user/.config/i3/config"
check "the home belongs to $user" owned_by_user "$root/home/$user"
check "the password is not expired (VD-10)" test "$(awk -F: -v name="$user" '$1 == name {print $3}' "$root/etc/shadow")" != 0
check "no stick repository left in pacman.conf" absent grep -q '^\[offline' "$root/etc/pacman.conf"
check "the mirrorlist is restored" absent grep -q imprint-no-mirror "$root/etc/pacman.d/mirrorlist"
check "no stand-in databases left" absent test -e "$root/var/lib/pacman/sync/.imprint-stand-ins"
check "no build sudo left" absent test -e "$root/etc/sudoers.d/zz-imprint-build"
check "no password file left" absent test -e "$root/root/.imprint-password"
check "no bind mount points left" absent bash -c "ls -d $root/var/lib/imprint-* 2>/dev/null | grep -q ."
check "GRUB on the removable path" test -f "$root/boot/EFI/BOOT/BOOTX64.EFI"
check "GRUB with its own entry" test -f "$root/boot/EFI/imprint/grubx64.efi"
check "grub.cfg written" test -s "$root/boot/grub/grub.cfg"
check "the firstboot unit ran" test -f "$root/var/log/imprint/firstboot.log"
check "$user logged in through the display manager (PAM session opened)" journal_has "pam_unix\((lightdm|sddm|gdm-password):session\): session opened for user $user"
check "no expired-password refusal at the greeter" absent journal_has "(password expired|authentication token is no longer valid|You are required to change your password)"
journalctl -D "$root/var/log/journal" --no-pager -o short | grep -E 'lightdm|pam_unix|firstboot' | tail -40

echo "$failures check(s) failed"
echo $((failures > 0 ? 1 : 0)) > /run/t/verify.exit
umount -R "$root"
sync
