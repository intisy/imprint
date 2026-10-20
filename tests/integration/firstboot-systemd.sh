#!/bin/sh
# Boots a systemd container three times: the firstboot hooks fail, run again and succeed, then never run again.
set -eu

repository_root=$(cd "$(dirname "$0")/../.." && (pwd -W 2>/dev/null || pwd))
image=imprint-firstboot-test
container=imprint-firstboot-test-$$
fixture=/work/tests/integration/firstboot
engine=/work/build/musl-firstboot/src/engine/imprint-engine

fail() {
    echo "FAIL: $*" >&2
    docker logs "$container" 2>&1 | tail -n 40 >&2
    docker exec "$container" journalctl --no-pager -u imprint-firstboot.service >&2 || true
    exit 1
}

inside() {
    MSYS_NO_PATHCONV=1 docker exec "$container" sh -c "$1"
}

wait_for_boot() {
    state=$(inside 'timeout 120 systemctl is-system-running --wait' || true)
    echo "booted: $state"
}

count_runs() {
    inside 'wc -l < /var/tmp/count-runs'
}

sh "$repository_root/tools/in-container.sh" sh -c \
    'cmake -S . -B build/musl-firstboot -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/musl.cmake \
         -DIMPRINT_STATIC=ON >/dev/null && cmake --build build/musl-firstboot --target imprint-engine >/dev/null'
docker build --quiet --tag "$image" "$repository_root/tests/integration/firstboot" >/dev/null

trap 'docker rm --force "$container" >/dev/null 2>&1 || true' EXIT
first_start="[ -e /var/tmp/installed ] || { $engine hooks install-firstboot $fixture/setup.json --data $fixture/data \
--personal $fixture/personal --target / && touch /var/tmp/installed; } && exec /lib/systemd/systemd"
MSYS_NO_PATHCONV=1 docker run --detach --name "$container" --privileged --cgroupns=private \
    --tmpfs /run --tmpfs /run/lock --volume "$repository_root:/work:ro" "$image" sh -c "$first_start" >/dev/null

wait_for_boot
inside 'systemctl is-failed --quiet imprint-firstboot.service' || fail "the first boot's failing hook did not fail the unit"
inside 'test -f /var/lib/imprint/firstboot/run.sh' || fail "the bundle was removed although a hook failed"
inside 'grep -q "hooks/flaky.sh failed with exit code 3" /var/log/imprint/firstboot.log' || fail "the failure is not logged"
[ "$(count_runs)" = 1 ] || fail "count.sh ran $(count_runs) times on the first boot"
echo "boot 1: the unit failed, kept its bundle and logged the failure"

inside 'touch /var/tmp/allow-success'
docker restart "$container" >/dev/null
wait_for_boot
inside 'grep -q "stage=firstboot key=tskey-fixture-not-a-real-key" /var/tmp/flaky-succeeded' || fail "the hook did not succeed on the second boot"
[ "$(count_runs)" = 2 ] || fail "count.sh ran $(count_runs) times after the second boot"
inside 'test ! -e /etc/systemd/system/imprint-firstboot.service' || fail "the unit file is still there"
inside 'test ! -e /etc/systemd/system/multi-user.target.wants/imprint-firstboot.service' || fail "the unit is still enabled"
inside 'test ! -e /var/lib/imprint/firstboot' || fail "the bundle and its personal copy are still there"
inside 'grep -q "every firstboot hook of setup .boot-test. succeeded" /var/log/imprint/firstboot.log' || fail "the success is not logged"
echo "boot 2: the hooks ran again, succeeded, and the unit removed itself"

docker restart "$container" >/dev/null
wait_for_boot
[ "$(count_runs)" = 2 ] || fail "count.sh ran again on the third boot"
echo "boot 3: nothing ran"
echo "PASS: firstboot under systemd"
