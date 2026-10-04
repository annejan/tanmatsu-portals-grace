#!/usr/bin/env bash
# badgelink.sh, tried up to three times: a transfer right after a USB mode
# switch can fail while the device is still enumerating.
here="$(cd "$(dirname "$0")/.." && pwd)"
for i in 1 2 3; do
    "$here/badgelink/tools/badgelink.sh" "$@" && exit 0
    sleep 2
done
exit 1
