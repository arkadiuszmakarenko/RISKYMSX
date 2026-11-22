#!/usr/bin/env bash
set -euo pipefail

readonly address=0x1fff8000
readonly size=28672
readonly mode=${1:?Missing operation}
programmer=${MINICHLINK:-minichlink}
readonly backup_root=${BACKUP_DIR:-"$PWD/backups"}

fail() { echo "Error: $*" >&2; exit 1; }

programmer_path=$(command -v "$programmer") || fail "Cannot find minichlink: $programmer"

# Older minichlink releases do not implement CH32V30x boot-region erase.
# A locally built programmer is used instead when one is available.
if ! strings "$programmer_path" | grep -F 'Curent data in BOOTLOADER will be rewritten.' >/dev/null; then
    local_programmer="$PWD/tools/minichlink"
    if [ -x "$local_programmer" ] && \
       strings "$local_programmer" | grep -F 'Curent data in BOOTLOADER will be rewritten.' >/dev/null; then
        echo "Using the locally built programmer: $local_programmer"
        programmer=$local_programmer
        programmer_path=$local_programmer
    else
        fail "This minichlink lacks CH32V20x/30x boot-region support. Run 'make setup-programmer' or set MINICHLINK to a compatible build."
    fi
fi

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

"$programmer" -i 2>&1 | tee "$tmp/chip-info"
grep -qx 'Detected CH32V303' "$tmp/chip-info" ||
    fail "Expected CH32V303 identification; no boot flash operation was attempted."
grep -qx 'Read protection: disabled' "$tmp/chip-info" ||
    fail "Read protection is enabled or its status could not be established."

check_image() {
    local file=$1
    [[ -f "$file" && $(wc -c < "$file") -eq $size ]] ||
        fail "$file must be exactly $size bytes"
}

read_region() {
    "$programmer" -r "$1" "$address" "$size"
    check_image "$1"
}

save_backup() {
    mkdir -p -- "$backup_root"
    local dir
    dir=$(mktemp -d "$backup_root/boot-$(date +%Y%m%d-%H%M%S)-XXXXXXXX")
    echo "Saving boot-region backup in $dir"
    read_region "$dir/bootloader.bin"
    read_region "$tmp/second-read.bin"
    cmp -s "$dir/bootloader.bin" "$tmp/second-read.bin" ||
        fail "Boot-region reads differ; backup in $dir is not verified."
    # A uniform read is not evidence that the factory bootloader was recovered.
    python3 -c 'import pathlib, sys
d = pathlib.Path(sys.argv[1]).read_bytes()
if len(set(d)) == 1:
    sys.exit("Error: boot region is uniform; backup is not usable as a verified recovery image.")' \
        "$dir/bootloader.bin"
    (cd "$dir" && sha256sum bootloader.bin > SHA256SUMS)
    echo "Two matching reads saved; keep $dir and its checksum somewhere safe."
    echo "Matching reads do not prove that the image is the factory bootloader or that restore works."
}

confirm_write() {
    echo "WARNING: this erases the entire 28 KiB system boot region and removes factory ISP."
    echo "A WCH-Link and a separately verified backup are required for attempted recovery."
    read -r -p "Type REPLACE BOOTLOADER to continue: " confirmation ||
        fail "No confirmation received; no boot flash write attempted."
    [[ $confirmation == "REPLACE BOOTLOADER" ]] || fail "Cancelled; no boot flash write attempted."
}

write_and_verify() {
    local image=$1
    check_image "$image"
    confirm_write
    echo "The programmer will also ask for confirmation; press Enter when prompted."
    "$programmer" -w "$image" "$address"
    read_region "$tmp/readback.bin"
    cmp -s "$image" "$tmp/readback.bin" ||
        fail "Read-back does not match. Do not rely on ISP; retain the backup and WCH-Link."
    echo "Boot-region read-back matches the image."
}

case "$mode" in
    backup)
        save_backup
        ;;
    flash)
        image=${2:?Missing image}
        check_image "$image"
        save_backup
        write_and_verify "$image"
        ;;
    restore)
        image=${2:?Missing backup}
        check_image "$image"
        backup_dir=$(cd "$(dirname "$image")" && pwd)
        [[ $(basename "$image") == bootloader.bin && -f "$backup_dir/SHA256SUMS" ]] ||
            fail "Restore requires bootloader.bin and SHA256SUMS from a backup directory."
        (cd "$backup_dir" && sha256sum bootloader.bin | cmp -s - SHA256SUMS) ||
            fail "Backup checksum verification failed."
        write_and_verify "$image"
        ;;
    *)
        fail "Unknown operation: $mode"
        ;;
esac
