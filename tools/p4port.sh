#!/usr/bin/env bash
# The Tanmatsu's ESP32-P4 debug console: print its tty, or fail.
#
#   tools/p4port.sh              print the P4's console
#   tools/p4port.sh --check TTY  print TTY if it is the P4's console, else fail
#   tools/p4port.sh --learn      only remember the P4's USB port (BadgeLink
#                                mode); prints nothing
#
# A Tanmatsu shows two Espressif USB-serial/JTAG units (303a:1001) behind
# its internal hub once the P4 is in debug mode: the P4's and the ESP32-C6
# radio coprocessor's. Opening the C6's resets the radio and crashes the
# running badge, so "the first /dev/ttyACM*" is never good enough.
#
# What tells them apart is the hub port. In BadgeLink mode the P4 is the
# 16d0:0f9a device; its port is remembered (build/.p4_usb_port), and in
# debug mode the P4's tty is the one on that same port. No remembered port:
# refuse rather than guess. The answer is the tty's /dev/serial/by-path
# name where there is one: that name only ever means that one port, while
# ttyACM numbers move about whenever the badge restarts the C6.
#
# Second guard: the serial numbers of the Espressif units beside the P4 on
# its hub in BadgeLink mode -- the C6 -- are remembered too, and a tty with
# one of those is refused even on the P4's port. A serial once accepted on
# the P4's port is never put on that list.
#
# Only /sys and /dev names are read; no serial port is ever opened.
# (P4PORT_TEST_ROOT is tests/p4port_test.sh's fake /sys, /dev and build/.)
set -eu
root="${P4PORT_TEST_ROOT:-}"
dir="${root:-$(cd "$(dirname "$0")/.." && pwd)}/build"
cache="$dir/.p4_usb_port"
others="$dir/.not_p4_serials"
mine="$dir/.p4_serials"

attr() { cat "$1/$2" 2>/dev/null || true; }
listed() { [[ -n "$1" ]] && grep -qxF -- "$1" "$2" 2>/dev/null; }

# BadgeLink mode: remember the P4's port, and the serials of the Espressif
# units on the same hub. Nothing happens without a 16d0:0f9a device.
learn() {
    local dev name s p4=""
    for dev in "$root"/sys/bus/usb/devices/*; do
        if [[ "$(attr "$dev" idVendor):$(attr "$dev" idProduct)" == "16d0:0f9a" ]]; then
            p4="$(basename "$dev")"
        fi
    done
    [[ -n "$p4" ]] || return 0
    mkdir -p "$dir"
    echo "$p4" > "$cache"
    [[ "$p4" == *.* ]] || return 0  # not behind a hub: no neighbours
    for dev in "$root"/sys/bus/usb/devices/*; do
        name="$(basename "$dev")"
        [[ "$(attr "$dev" idVendor)" == "303a" && "$name" != "$p4" && "${name%.*}" == "${p4%.*}" ]] || continue
        s="$(attr "$dev" serial)"
        [[ -n "$s" ]] || continue
        listed "$s" "$mine" && continue
        listed "$s" "$others" || echo "$s" >> "$others"
    done
    return 0
}

# Debug mode: the tty on the remembered port.
find_tty() {
    if [[ ! -f "$cache" ]]; then
        echo "p4port: the P4's USB port is not known yet: with the badge in BadgeLink mode, run 'make install' or 'tools/p4port.sh --learn' once" >&2
        return 1
    fi
    local port tty usbdev serial link
    port="$(cat "$cache")"
    for tty in "$root"/sys/class/tty/ttyACM*; do
        [[ -e "$tty" ]] || continue
        # .../<port>/<port>:1.0/tty/ttyACMn -> the USB device is two levels up
        usbdev="$(basename "$(dirname "$(readlink -f "$tty/device")")")"
        [[ "$usbdev" == "$port" ]] || continue
        serial="$(attr "$root/sys/bus/usb/devices/$usbdev" serial)"
        if listed "$serial" "$others" && ! listed "$serial" "$mine"; then
            echo "p4port: the tty on the P4's port has serial $serial, seen beside the P4 before: not the P4" >&2
            echo "p4port: if it is the P4 after all, delete $others" >&2
            return 1
        fi
        if [[ -n "$serial" ]] && ! listed "$serial" "$mine"; then echo "$serial" >> "$mine"; fi
        for link in "$root"/dev/serial/by-path/*; do
            if [[ -e "$link" && "$(readlink -f "$link")" == "$root/dev/$(basename "$tty")" ]]; then
                echo "$link"
                return 0
            fi
        done
        echo "$root/dev/$(basename "$tty")"
        return 0
    done
    echo "p4port: no tty on the P4's port ($port). Is the badge in debug mode? If it is on another USB port now, put it in BadgeLink mode once ('make install') to learn the new one." >&2
    return 1
}

# An explicit console: only if it is the one find_tty finds. A forwarded
# one (rfc2217://host:port) is the far side's business, and taken as given.
check() {
    local want="$1" p4
    if [[ "$want" == *://* ]]; then
        echo "$want"
        return 0
    fi
    p4="$(find_tty)" || return 1
    if [[ "$(readlink -f "$want")" != "$(readlink -f "$p4")" ]]; then
        echo "p4port: $want is not the P4's console; $p4 is" >&2
        return 1
    fi
    echo "$want"
}

usage() { echo "usage: $0 [--learn | --check TTY]" >&2; exit 2; }

learn
case "${1:-}" in
    "") [[ $# -le 1 ]] || usage; find_tty ;;
    --learn) [[ $# -eq 1 ]] || usage ;;
    --check) [[ $# -eq 2 ]] || usage; check "$2" ;;
    *) usage ;;
esac
