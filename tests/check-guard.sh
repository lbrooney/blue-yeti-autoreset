#!/bin/sh

set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temp_dir=$(mktemp -d)
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

marker=$temp_dir/marker
mkdir -p "$temp_dir/sysfs/1-2.1"
printf '%s\n' test-serial > "$temp_dir/sysfs/1-2.1/serial"
first_output=$(
    BLUE_YETI_RESET_TOOL=/usr/bin/true \
    BLUE_YETI_MARKER="$marker" \
    BLUE_YETI_DEVICE=1-2.1 \
    BLUE_YETI_SYSFS_ROOT="$temp_dir/sysfs" \
    "$repo_dir/scripts/blue-yeti-autoreset"
)
test -f "$marker"
test "$first_output" = "Automatic Blue Yeti reset accepted for serial test-serial."

second_output=$(
    BLUE_YETI_RESET_TOOL=/usr/bin/true \
    BLUE_YETI_MARKER="$marker" \
    BLUE_YETI_DEVICE=1-2.1 \
    BLUE_YETI_SYSFS_ROOT="$temp_dir/sysfs" \
    "$repo_dir/scripts/blue-yeti-autoreset"
)
test ! -e "$marker"
test "$second_output" = "Skipping the expected post-reset re-enumeration."

printf '%s\n' 0 > "$marker"
BLUE_YETI_RESET_TOOL=/usr/bin/true \
BLUE_YETI_MARKER="$marker" \
BLUE_YETI_SERIAL=test-serial \
"$repo_dir/scripts/blue-yeti-autoreset" >/dev/null
test -f "$marker"

rm -f "$marker"
if BLUE_YETI_RESET_TOOL=/usr/bin/false \
    BLUE_YETI_MARKER="$marker" \
    BLUE_YETI_SERIAL=test-serial \
    "$repo_dir/scripts/blue-yeti-autoreset" 2>/dev/null; then
    exit 1
fi
test ! -e "$marker"

if BLUE_YETI_RESET_TOOL=/usr/bin/true \
    BLUE_YETI_MARKER="$marker" \
    BLUE_YETI_SERIAL=test-serial \
    BLUE_YETI_SUPPRESS_SECONDS=invalid \
    "$repo_dir/scripts/blue-yeti-autoreset" 2>/dev/null; then
    exit 1
fi
test ! -e "$marker"
