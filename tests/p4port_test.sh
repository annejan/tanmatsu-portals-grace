#!/usr/bin/env bash
# tools/p4port.sh against fake USB trees: it must find the P4's console,
# and never answer with the C6's tty. Only files under a scratch directory
# are made; nothing real is read or opened. `make check`.
set -u
here="$(cd "$(dirname "$0")/.." && pwd)"
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
export P4PORT_TEST_ROOT="$root"
p4port="$here/tools/p4port.sh"
fails=0

# topo port,vid,pid,serial[,ttyN] ...: a fresh fake /sys and /dev (build/
# with what p4port remembers is kept). The by-path names follow the port.
topo() {
    rm -rf "$root/sys" "$root/dev"
    mkdir -p "$root/sys/bus/usb/devices" "$root/sys/class/tty" "$root/dev/serial/by-path" "$root/build"
    local spec port vid pid serial tty real
    for spec in "$@"; do
        IFS=, read -r port vid pid serial tty <<< "$spec"
        real="$root/sys/devices/pci0000:00/usb/$port"
        mkdir -p "$real/$port:1.0"
        echo "$vid" > "$real/idVendor"
        echo "$pid" > "$real/idProduct"
        [[ -n "$serial" ]] && echo "$serial" > "$real/serial"
        ln -s "$real" "$root/sys/bus/usb/devices/$port"
        if [[ -n "$tty" ]]; then
            mkdir -p "$root/sys/class/tty/$tty"
            ln -s "$real/$port:1.0" "$root/sys/class/tty/$tty/device"
            touch "$root/dev/$tty"
            ln -s "../../$tty" "$root/dev/serial/by-path/pci-0000:00:14.0-usb-0:${port#*-}:1.0"
        fi
    done
}

by_path() { echo "$root/dev/serial/by-path/pci-0000:00:14.0-usb-0:${1#*-}:1.0"; }

# expect <exit code> <stdout> <what> -- <p4port args>
expect() {
    local code="$1" out="$2" what="$3" got rc
    shift 4
    got="$("$p4port" "$@" 2> /dev/null)"
    rc=$?
    if [[ "$rc" != "$code" || "$got" != "$out" ]]; then
        echo "FAIL p4port $*: $what: exit $rc, \"$got\" (wanted exit $code, \"$out\")"
        fails=$((fails + 1))
    fi
}

serials() { tr '\n' ' ' < "$root/build/$1" 2> /dev/null; }

# The badge in BadgeLink mode: the P4 is 16d0:0f9a on hub port .3, the C6
# a 303a unit on .1. The port and the C6's serial are learned.
topo 3-2.3,16d0,0f9a,,  3-2.1,303a,1001,C6,ttyACM0
expect 1 "" "BadgeLink mode has no P4 console" --
[[ "$(cat "$root/build/.p4_usb_port")" == "3-2.3" ]] || { echo "FAIL: P4 port not learned"; fails=$((fails + 1)); }
[[ "$(serials .not_p4_serials)" == "C6 " ]] || { echo "FAIL: C6 serial not learned: $(serials .not_p4_serials)"; fails=$((fails + 1)); }

# Debug mode: both are 303a units now; only the one on the P4's port does.
topo 3-2.3,303a,1001,P4,ttyACM1  3-2.1,303a,1001,C6,ttyACM0
expect 0 "$(by_path 3-2.3)" "debug mode: the P4's console, by path" --
expect 1 "" "the C6's tty is refused" -- --check "$root/dev/ttyACM0"
expect 1 "" "the C6's by-path name is refused" -- --check "$(by_path 3-2.1)"
expect 0 "$root/dev/ttyACM1" "the P4's tty passes" -- --check "$root/dev/ttyACM1"
expect 1 "" "a missing device is refused" -- --check "$root/dev/ttyACM9"
expect 0 "rfc2217://host:4000" "a forwarded console is taken as given" -- --check rfc2217://host:4000

# The device-test tools ask p4port before they open anything (with
# pyserial about; it is only imported, nothing real is opened).
if python3 -c "import serial" 2> /dev/null; then
    out="$(python3 "$here/tools/recover.py" --port "$root/dev/ttyACM0" 2>&1)"
    [[ $? == 1 && "$out" == *"not the P4's console"* ]] || { echo "FAIL: recover.py reset the C6's tty: $out"; fails=$((fails + 1)); }
    out="$(python3 "$here/tools/testrun.py" --port "$root/dev/ttyACM0" --out-dir "$root/results" perf 2>&1)"
    [[ $? == 5 && "$out" == *"refusing"* ]] || { echo "FAIL: testrun.py took the C6's tty: $out"; fails=$((fails + 1)); }
    out="$(PORT="$root/dev/ttyACM1" python3 "$here/tools/recover.py" 2>&1)"
    [[ $? == 2 ]] || { echo "FAIL: recover.py read \$PORT: $out"; fails=$((fails + 1)); }
    out="$(cd "$here/tools" && python3 -c "import testrun; testrun.check_console('$root/dev/ttyACM1'); print('ok')" 2>&1)"
    [[ "$out" == ok ]] || { echo "FAIL: the P4's tty refused by testrun: $out"; fails=$((fails + 1)); }
fi

# The ttyACM numbers swap (the C6 restarted): still the P4.
topo 3-2.3,303a,1001,P4,ttyACM0  3-2.1,303a,1001,C6,ttyACM1
expect 0 "$(by_path 3-2.3)" "after the numbers swap" --
expect 1 "" "ttyACM1 is the C6 now" -- --check "$root/dev/ttyACM1"

# Re-plugged elsewhere in debug mode: the old port has no tty; refuse,
# and learn nothing (there is no BadgeLink device to learn from).
topo 3-4.3,303a,1001,P4,ttyACM0  3-4.1,303a,1001,C6,ttyACM1
expect 1 "" "on another USB port, before BadgeLink mode" --
[[ "$(serials .not_p4_serials)" == "C6 " ]] || { echo "FAIL: learned in debug mode: $(serials .not_p4_serials)"; fails=$((fails + 1)); }

# Two badges, one in each mode: only the neighbours of the BadgeLink one
# are learned, never the other badge's P4.
topo 3-2.3,16d0,0f9a,,  3-2.1,303a,1001,C6,ttyACM0  3-4.3,303a,1001,P4B,ttyACM1  3-4.1,303a,1001,C6B,ttyACM2
expect 0 "" "learn" -- --learn
[[ "$(serials .not_p4_serials)" == "C6 " ]] || { echo "FAIL: another badge's units learned: $(serials .not_p4_serials)"; fails=$((fails + 1)); }

# A serial once accepted on the P4's port is never learned as "not the P4",
# even if it ever shows up beside a BadgeLink device.
topo 3-2.3,16d0,0f9a,,  3-2.1,303a,1001,P4,
expect 0 "" "learn" -- --learn
[[ "$(serials .not_p4_serials)" == "C6 " ]] || { echo "FAIL: the P4's serial was learned: $(serials .not_p4_serials)"; fails=$((fails + 1)); }

# A unit whose serial is the C6's, on the P4's port: refused.
topo 3-2.3,303a,1001,C6,ttyACM0
expect 1 "" "the C6's serial on the P4's port" --

# Nothing learned yet: refuse rather than guess.
rm -rf "$root/build"
topo 3-2.3,303a,1001,P4,ttyACM1  3-2.1,303a,1001,C6,ttyACM0
expect 1 "" "no port learned yet" --
expect 1 "" "and no check passes" -- --check "$root/dev/ttyACM1"
expect 2 "" "bad arguments" -- --bogus

if [[ $fails -gt 0 ]]; then
    echo "p4port tests: $fails failed"
    exit 1
fi
echo "p4port tests: all passed"
