#!/bin/sh

set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

make_device()
{
    device=$1
    vendor=$2
    product=$3
    firmware=$4
    serial=$5

    mkdir -p "$temp_dir/sysfs/$device"
    printf '%s\n' "$vendor" > "$temp_dir/sysfs/$device/idVendor"
    printf '%s\n' "$product" > "$temp_dir/sysfs/$device/idProduct"
    printf '%s\n' "$firmware" > "$temp_dir/sysfs/$device/bcdDevice"
    printf '%s\n' "$serial" > "$temp_dir/sysfs/$device/serial"
}

mkdir -p "$temp_dir/empty-sysfs"
empty_output=$(
    BLUE_YETI_RESET_TOOL=/usr/bin/true \
    BLUE_YETI_SYSFS_ROOT="$temp_dir/empty-sysfs" \
    "$repo_dir/scripts/blue-yeti-autoreset"
)
test "$empty_output" = "No supported Blue Yeti found; nothing to reset."

make_device 1-1 046d 0ab7 0020 first-serial
make_device 1-2 046d 0ab7 0021 wrong-firmware
make_device 1-3 046d 0ab8 0020 wrong-product
make_device 1-4 1234 0ab7 0020 wrong-vendor
make_device 1-5 046d 0ab7 0020 second-serial

output=$(
    BLUE_YETI_RESET_TOOL=/usr/bin/true \
    BLUE_YETI_SYSFS_ROOT="$temp_dir/sysfs" \
    "$repo_dir/scripts/blue-yeti-autoreset"
)
expected_output='Automatic Blue Yeti reset accepted for serial first-serial.
Automatic Blue Yeti reset accepted for serial second-serial.'
test "$output" = "$expected_output"

if BLUE_YETI_RESET_TOOL=/usr/bin/false \
    BLUE_YETI_SYSFS_ROOT="$temp_dir/sysfs" \
    "$repo_dir/scripts/blue-yeti-autoreset" 2>/dev/null; then
    exit 1
fi
