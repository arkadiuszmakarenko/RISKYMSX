#!/usr/bin/env python3
"""
Create a proper bootable 720K floppy image from the Nextor tools disk.

The vendor nextor.dsk is a tools disk (partition dump), not a floppy:
its hidden_sectors field carries the partition start LBA, and the kernel
would compute cluster->LBA with that and look for files past the end of
a 1440-sector floppy. This script:

  - re-asserts the BPB hidden_sectors = 0 (the BPB write order in
    create_boot_sector is BPB LAST, so the vendor's partition-start
    bytes at 0x1E..0x1F get overwritten);
  - rebuilds a clean FAT12 filesystem with EVERY non-deleted file the
    vendor's root directory contains, contiguous on disk in the order
    they appear (the original layout was already contiguous, so this
    is byte-identical to a sector-copy of the data area);
  - keeps the vendor's MSX-DOS 2.20 boot code body and VOL_ID block.

The output image has the same 28 .COM/.SYS tools plus NEXTOR.SYS and
COMMAND3.COM, totaling 30 entries in the root directory. NEXTOR.SYS and
COMMAND3.COM get system+hidden attribute set (0x27) so the kernel's
TRY_MSX_DOS FCB open accepts them as system files.
"""
import struct
from pathlib import Path

VENDOR_DSK = Path("/home/makaron/Repo/RISKYMSX2/MSXSoftware/NextorDriver/vendor/nextor.dsk")
OUTPUT_DSK = Path("/home/makaron/Repo/RISKYMSX2/MSXSoftware/NextorDriver/nextor_boot.dsk")

SECTOR_SIZE       = 512
TOTAL_SECTORS     = 1440   # 720 KiB
SECTORS_PER_CLUSTER = 2
RESERVED_SECTORS  = 1
NUM_FATS          = 2
ROOT_ENTRIES      = 112
SECTORS_PER_FAT   = 3
SECTORS_PER_TRACK = 9
HEADS             = 2
MEDIA_DESC        = 0xF9

# Names that get the system+hidden attribute forced on (everything else
# keeps the vendor's attribute, which is 0x20 = archive for the .COM
# tools). Both spellings because root entries are 8+3 with the ext
# padded to 3, so 'COMMAND3COM' is COMMAND3.COM and 'NEXTOR  SYS' is
# NEXTOR.SYS.
SYSTEM_FILES = (b'NEXTOR  SYS', b'COMMAND3COM', b'NEXTORJ SYS')


def parse_fat12(fat_data):
    """Decode a FAT12 byte stream into a list of cluster successors."""
    out = []
    for i in range(0, len(fat_data), 3):
        if i + 2 >= len(fat_data):
            break
        b0, b1, b2 = fat_data[i], fat_data[i+1], fat_data[i+2]
        c1 = b0 | ((b1 & 0x0F) << 8)
        c2 = (b1 >> 4) | (b2 << 4)
        out.append(c1)
        out.append(c2)
    return out


def set_fat12(fat, cluster, value):
    """Write a 12-bit FAT12 entry."""
    offset = cluster + (cluster // 2)
    if cluster & 1:
        fat[offset]     = (fat[offset] & 0x0F) | ((value & 0x0F) << 4)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset]     = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def read_vendor_disk():
    """Return (layout, root_entries, fat_clusters, file_data).

    layout         dict with bpb fields, fat/root/data offsets.
    root_entries   list of dicts: name (8+3 bytes), attr, cluster, size.
                   LFN entries (attr 0x0F) are dropped, deletions
                   (0xE5 first byte) are dropped, the volume label is
                   dropped, and the directory is terminated at the
                   first 0x00.
    fat_clusters   list of cluster successors.
    file_data      dict name (8+3) -> bytes for every regular file.
    """
    with open(VENDOR_DSK, 'rb') as f:
        data = f.read()

    bpb_bytes_per_sec = struct.unpack_from('<H', data, 0x0B)[0]
    bpb_spc           = data[0x0D]
    bpb_reserved      = struct.unpack_from('<H', data, 0x0E)[0]
    bpb_fats          = data[0x10]
    bpb_root_entries  = struct.unpack_from('<H', data, 0x11)[0]
    bpb_spf           = struct.unpack_from('<H', data, 0x16)[0]

    fat_start   = bpb_reserved
    fat_size    = bpb_fats * bpb_spf
    root_start  = fat_start + fat_size
    root_sectors = (bpb_root_entries * 32 + bpb_bytes_per_sec - 1) // bpb_bytes_per_sec
    data_start  = root_start + root_sectors

    layout = dict(
        bpb_bytes_per_sec = bpb_bytes_per_sec,
        bpb_spc           = bpb_spc,
        root_start        = root_start,
        root_sectors      = root_sectors,
        data_start        = data_start,
        fat_sectors       = bpb_spf,
        fat1_off          = fat_start * bpb_bytes_per_sec,
    )

    root_off_bytes = root_start * bpb_bytes_per_sec
    root_data = data[root_off_bytes : root_off_bytes + root_sectors * bpb_bytes_per_sec]

    fat1 = data[fat_start * bpb_bytes_per_sec : (fat_start + bpb_spf) * bpb_bytes_per_sec]
    fat_clusters = parse_fat12(fat1)

    # Walk root directory, skip LFN/volume-label/deleted/zero.
    root_entries = []
    for i in range(bpb_root_entries):
        entry = root_data[i*32 : (i+1)*32]
        if not entry:
            break
        if entry[0] == 0x00:
            break
        if entry[0] == 0xE5:
            continue
        attr = entry[11]
        if attr & 0x0F == 0x0F:
            continue            # LFN entry
        if attr & 0x08:
            continue            # volume label
        name = bytes(entry[0:11])
        if name == b'.          ' or name == b'..         ':
            continue            # "." / ".." (none on a flat root, but safe)
        cluster = struct.unpack_from('<H', entry, 26)[0]
        size    = struct.unpack_from('<I', entry, 28)[0]
        root_entries.append(dict(name=name, attr=attr, cluster=cluster, size=size))

    # Read each file's bytes through its cluster chain.
    def read_chain(cluster, size):
        out = bytearray()
        while cluster >= 2 and cluster < 0xFF8 and len(out) < size:
            sector = data_start + (cluster - 2) * bpb_spc
            for s in range(bpb_spc):
                offset = (sector + s) * bpb_bytes_per_sec
                take   = min(bpb_bytes_per_sec, size - len(out))
                if take <= 0:
                    break
                out.extend(data[offset : offset + take])
            cluster = fat_clusters[cluster]
        return bytes(out[:size])

    file_data = {}
    for e in root_entries:
        if e['size'] > 0:
            file_data[e['name']] = read_chain(e['cluster'], e['size'])
        else:
            file_data[e['name']] = b''

    return layout, root_entries, fat_clusters, file_data


def create_boot_sector():
    """Build a clean 720K floppy boot sector with the vendor's MSX-DOS 2.20
    boot body intact.

    Layout:
      0x00..0x0A: jump + OEM (we write)
      0x0B..0x1D: BPB (we write, gets kept - vendor copy starts at 0x1E)
      0x1E..0x1FF: vendor's MSX-DOS 2.20 boot body (copied verbatim -
                   the inner JR at 0x1E, the VOL_ID marker at 0x20,
                   the BOOT_BODY at 0x30)
      0x1FE..0x1FF: zeros (no PC boot signature)

    Why we don't touch hidden_sectors: the kernel ignores it for
    non-partitioned disks - it computes data area as
    reserved + 2*FAT_size + root_size, all relative to sector 0. The
    vendor's 0x10180000 there is harmless. Zeroing it would corrupt the
    inner JR instruction (0x18 0x10) that lives at exactly those bytes.
    """
    boot = bytearray(SECTOR_SIZE)

    # BPB (0x00..0x1D). The vendor copy below starts at 0x1E, so all of
    # this survives intact.
    boot[0:3]   = b'\xEB\x3C\x90'                       # jump to BOOT_BODY at 0x3E
    boot[3:11]  = b'NEXTOR3 '                           # OEM ID
    boot[11:13] = struct.pack('<H', SECTOR_SIZE)         # bytes per sector
    boot[13]    = SECTORS_PER_CLUSTER                    # sectors per cluster
    boot[14:16] = struct.pack('<H', RESERVED_SECTORS)    # reserved sectors
    boot[16]    = NUM_FATS                               # number of FATs
    boot[17:19] = struct.pack('<H', ROOT_ENTRIES)        # root entries
    boot[19:21] = struct.pack('<H', TOTAL_SECTORS)       # total sectors (16-bit)
    boot[21]    = MEDIA_DESC                             # media descriptor
    boot[22:24] = struct.pack('<H', SECTORS_PER_FAT)     # sectors per FAT
    boot[24:26] = struct.pack('<H', SECTORS_PER_TRACK)   # sectors per track
    boot[26:28] = struct.pack('<H', HEADS)               # heads
    boot[28:32] = struct.pack('<I', 0)                    # hidden sectors low 16 bits (0x1C..0x1D) - the
                                                    # high 16 bits (0x1E..0x1F) are overwritten by the
                                                    # vendor body copy below, which is the inner JR and
                                                    # must NOT be zeroed (see create_boot_sector)

    # Vendor's MSX-DOS 2.20 boot body at 0x1E..0x1FE, copied verbatim.
    # The vendor's layout (Nextor SDK bootsect.mac, .phase 0C01Eh):
    #   0x1E..0x1F: inner JR to BOOT_BODY at 0x30 (BOOTAD+1Eh entry).
    #                Byte values 0x18 0x10 are the JR +0x10 opcode
    #                pair; they also happen to be the upper 16 bits
    #                of the vendor's hidden_sectors (because the
    #                vendor image is a PARTITION dump and that field
    #                carries 0x10180000 LE). DO NOT touch these bytes.
    #   0x20..0x25: "VOL_ID\0" - BSEC_TYPE's DOS-2.20 marker.
    #   0x26..0x2F: dirty flag + volume id + reserved zeros.
    #   0x30..    : BOOT_BODY ("RET NC" + the loader code, ending with
    #                the BIOS calls that locate NEXTOR.SYS).
    with open(VENDOR_DSK, 'rb') as f:
        vendor = f.read()
    boot[0x1E:0x200] = vendor[0x1E:0x200]

    # No boot signature at 0x1FE. MSX-DOS 1, MSX-DOS 2, and Nextor boot
    # sectors all leave 0x1FE..0x1FF at zero - the 0x55 0xAA marker is
    # for PC-style ("standard") boot sectors and would tell the kernel
    # to skip step 3 of the boot procedure (the one that runs the boot
    # code).
    # boot[0x1FE:0x200] = b'\x55\xAA'  # intentionally NOT set

    return bytes(boot)


def build_filesystem(root_entries, file_data):
    """Allocate contiguous clusters in source order and build a clean
    FAT12 + root directory.

    The original vendor layout was already contiguous (each file's
    cluster chain is just "next, next, next, ..., EOC"), so this
    rebuild produces a byte-identical data area. Returning contiguous
    clusters also means the firmware's on-demand read path (which
    forwards cluster index to LBA linearly) doesn't care about
    fragmentation, even though the original was contiguous by luck.
    """
    fat_size_bytes = SECTORS_PER_FAT * SECTOR_SIZE
    fat = bytearray(fat_size_bytes)
    # Cluster 0 carries the media descriptor in the low 8 bits and 0xF
    # in the high 4 (per FAT spec). Cluster 1 is EOC.
    set_fat12(fat, 0, 0xFF0 | MEDIA_DESC)
    set_fat12(fat, 1, 0xFFF)

    root_sectors = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE
    fat_start    = RESERVED_SECTORS
    root_start   = fat_start + NUM_FATS * SECTORS_PER_FAT
    data_start   = root_start + root_sectors
    total_data   = TOTAL_SECTORS - data_start
    total_clusters = total_data // SECTORS_PER_CLUSTER

    cluster = 2
    placements = []     # (name, attr, start_cluster, size)

    for e in root_entries:
        size = len(file_data[e['name']])
        if size > 0:
            sectors_needed = (size + SECTOR_SIZE - 1) // SECTOR_SIZE
            clusters_needed = (sectors_needed + SECTORS_PER_CLUSTER - 1) // SECTORS_PER_CLUSTER
            if cluster + clusters_needed - 2 >= total_clusters:
                # Image full: stop here so the kernel sees a well-formed
                # but truncated directory. Better than truncating a file
                # mid-cluster and leaving a phantom EOC.
                print(f"WARNING: out of clusters at '{e['name'].decode('latin-1', 'replace').rstrip()}' "
                      f"(cluster={cluster}, need {clusters_needed}, have {total_clusters - (cluster - 2)})")
                break
            start = cluster
            for i in range(clusters_needed - 1):
                set_fat12(fat, cluster, cluster + 1)
                cluster += 1
            set_fat12(fat, cluster, 0xFFF)
            cluster += 1
            placements.append((e['name'], e['attr'], start, size))

    # Build root directory.
    root_dir = bytearray(root_sectors * SECTOR_SIZE)
    for idx, (name, attr, start, size) in enumerate(placements):
        entry = bytearray(32)
        entry[0:11] = name
        # System files get 0x27 (system+hidden+archive) so the kernel's
        # TRY_MSX_DOS FCB open accepts them as system files. Everything
        # else keeps the vendor's attribute (0x20 = archive).
        if name in SYSTEM_FILES:
            entry[11] = 0x27
        else:
            entry[11] = attr
        # Date/time left zero: the firmware has no RTC and the kernel's
        # boot-time write of any directory entry will refresh them.
        entry[26:28] = struct.pack('<H', start)
        entry[28:32] = struct.pack('<I', size)
        root_dir[idx*32 : (idx+1)*32] = entry
    # Zero-termination is already there (bytearray starts zeroed); the
    # first entry past placements reads as 0x00 * 32 which signals EOD.

    return fat, root_dir, data_start, placements


def main():
    print("Reading vendor disk...")
    layout, root_entries, _, file_data = read_vendor_disk()
    print(f"  found {len(root_entries)} root entries ({sum(1 for e in root_entries if e['size'] > 0)} files, "
          f"{sum(e['size'] for e in root_entries)} bytes)")

    print("Creating boot sector...")
    boot_sector = create_boot_sector()

    print("Building FAT and directory...")
    fat, root_dir, data_start, placements = build_filesystem(root_entries, file_data)
    print(f"  placed {len(placements)} files starting at cluster 2")

    print("Writing output image...")
    with open(OUTPUT_DSK, 'wb') as f:
        f.write(boot_sector)
        f.write(fat)
        f.write(fat)                                       # FAT2 mirrors FAT1
        f.write(root_dir)

        data_area_size = (TOTAL_SECTORS - data_start) * SECTOR_SIZE
        data_area = bytearray(data_area_size)
        for name, _, start_cluster, size in placements:
            src   = file_data[name]
            cluster = start_cluster
            remaining = size
            src_off = 0
            while remaining > 0:
                lba        = data_start + (cluster - 2) * SECTORS_PER_CLUSTER
                rel_offset = (lba - data_start) * SECTOR_SIZE
                take       = min(remaining, SECTORS_PER_CLUSTER * SECTOR_SIZE)
                data_area[rel_offset : rel_offset + take] = src[src_off : src_off + take]
                src_off   += take
                remaining -= take
                cluster   += 1
        f.write(data_area)

    print(f"Done! Wrote {OUTPUT_DSK} ({TOTAL_SECTORS * SECTOR_SIZE} bytes)")
    print(f"Copy this to USB stick as NEXTOR.DSK (or RISKYMSX.DSK)")


if __name__ == '__main__':
    main()
