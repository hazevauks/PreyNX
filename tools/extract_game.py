#!/usr/bin/env python3
"""Extracts Prey's data from your own discs and patch.

Takes the .pk4 files out of the three disc images (ISO 9660) and out of the 1.4
patch installer, which stores its archives uncompressed, and leaves them all in
one folder. No other tools are needed, and nothing is installed.

    python tools/extract_game.py <folder with the .iso and setup files> <output base folder>
"""

import argparse
import mmap
import os
import pathlib
import struct
import sys

SECTOR = 2048
# patch archives the Switch build uses; game0x hold PC libraries, pak040 is the Japanese text
PATCH_ARCHIVES = ("pak005.pk4", "pak006.pk4", "pak020.pk4")


def iso_entries(iso, extent, size, prefix=""):
    """Yields (path, extent, size) for the files under a directory."""
    offset = 0
    while offset < size:
        iso.seek(extent * SECTOR + offset)
        record = iso.read(255)
        length = record[0] if record else 0
        if length == 0:
            offset = ( offset // SECTOR + 1 ) * SECTOR
            continue
        child_extent = struct.unpack("<I", record[2:6])[0]
        child_size = struct.unpack("<I", record[10:14])[0]
        flags = record[25]
        name_length = record[32]
        name = record[33:33 + name_length].decode("latin-1").split(";")[0]
        offset += length
        if name_length == 1 and record[33] in (0, 1):
            continue
        path = prefix + name
        if flags & 2:
            yield from iso_entries(iso, child_extent, child_size, path + "/")
        else:
            yield path, child_extent, child_size


def extract_iso(path, output):
    count = 0
    with open(path, "rb") as iso:
        iso.seek(16 * SECTOR)
        descriptor = iso.read(SECTOR)
        if descriptor[1:6] != b"CD001":
            raise SystemExit("%s is not an ISO 9660 image" % path)
        root = descriptor[156:190]
        root_extent = struct.unpack("<I", root[2:6])[0]
        root_size = struct.unpack("<I", root[10:14])[0]
        for name, extent, size in iso_entries(iso, root_extent, root_size):
            if not name.lower().endswith(".pk4"):
                continue
            target = output / name.rsplit("/", 1)[-1].lower()
            iso.seek(extent * SECTOR)
            with open(target, "wb") as out:
                remaining = size
                while remaining:
                    chunk = iso.read(min(remaining, 8 * 1024 * 1024))
                    if not chunk:
                        raise SystemExit("%s: truncated at %s" % (path, name))
                    out.write(chunk)
                    remaining -= len(chunk)
            print("  %s (%d MB)" % (target.name, size >> 20))
            count += 1
    return count


def extract_patch(path, output):
    """The installer lists its archives in order, and stores them as plain zip files."""
    with open(path, "rb") as f:
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        # the installer's file table: the names, zero separated, in the order they are stored
        table = data.find(b"game00.pk4")
        if table < 0:
            raise SystemExit("%s: this is not the expected 1.4 patch" % path)
        names = []
        for name in data[table:table + 256].split(bytes(1)):
            if not name.endswith(b".pk4"):
                break
            names.append(name.decode())

        archives = []
        position = 0
        while True:
            position = data.find(b"PK\x05\x06", position)
            if position < 0:
                break
            entries, directory_size, directory_offset, comment = struct.unpack("<HIIH", data[position + 10:position + 22])
            directory = position - directory_size
            if data[directory:directory + 4] == b"PK\x01\x02":
                archives.append((directory - directory_offset, position + 22 + comment))
            position += 4

        if len(archives) != len(names):
            raise SystemExit("%s: found %d archives for %d names; this is not the expected 1.4 patch" % (path, len(archives), len(names)))

        for name, (start, end) in zip(names, archives):
            if name in PATCH_ARCHIVES:
                with open(output / name, "wb") as out:
                    out.write(data[start:end])
                print("  %s (%d MB)" % (name, (end - start) >> 20))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="folder with the disc images and the patch")
    parser.add_argument("output", help="folder to write the .pk4 files to")
    args = parser.parse_args()

    source = pathlib.Path(args.source)
    output = pathlib.Path(args.output)
    output.mkdir(parents=True, exist_ok=True)

    images = sorted(source.glob("*.iso"))
    if not images:
        raise SystemExit("no .iso files in %s" % source)
    for image in images:
        print(image.name)
        extract_iso(image, output)

    for patch in sorted(source.glob("*.exe")):
        print(patch.name)
        extract_patch(patch, output)

    print("done: %d files in %s" % (len(list(output.glob("*.pk4"))), output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
