#!/usr/bin/env bash
# Print the tty of the Tanmatsu's ESP32-P4 debug console, or fail.
#
# A Tanmatsu shows two Espressif USB-serial/JTAG units (303a:1001) behind
# its internal hub once the P4 is in debug mode: the P4's and the ESP32-C6
# radio coprocessor's. Opening the C6's resets the radio and crashes the
# running badge, so "the first /dev/ttyACM*" is never good enough.
#
# What tells them apart is the hub port. In BadgeLink mode the P4 is the
# 16d0:0f9a device; its port is remembered (build/.p4_usb_port), and in
# debug mode the P4's tty is the one on that same port. No remembered
# port and no BadgeLink device: refuse rather than guess.
set -eu
cache="$(dirname "$0")/../build/.p4_usb_port"

for dev in /sys/bus/usb/devices/*; do
    [[ -f "$dev/idVendor" ]] || continue
    if [[ "$(cat "$dev/idVendor")" == "16d0" && "$(cat "$dev/idProduct")" == "0f9a" ]]; then
        mkdir -p "$(dirname "$cache")"
        basename "$dev" > "$cache"
    fi
done
[[ -f "$cache" ]] || { echo "p4port: P4 not seen in BadgeLink mode yet; run with the badge in BadgeLink mode first" >&2; exit 1; }
port="$(cat "$cache")"

for tty in /sys/class/tty/ttyACM*; do
    [[ -e "$tty" ]] || continue
    # .../<port>/<port>:1.0/tty/ttyACMn -> the USB device is two levels up
    usbdev="$(basename "$(dirname "$(readlink -f "$tty/device")")")"
    if [[ "$usbdev" == "$port" ]]; then
        echo "/dev/$(basename "$tty")"
        exit 0
    fi
done
echo "p4port: no tty on the P4's port ($port); is the badge in debug mode? (make mode_debug)" >&2
exit 1
