"""Lists or extracts files from the .sdf archives of an installed World in Conflict. Used to pull reference art out of the
user's own install into Art/Source/Local (gitignored): Massive's art is not ours to commit or ship.

  python Tools/Art/extract_sdf.py <archive.sdf> --list [pattern]
  python Tools/Art/extract_sdf.py <archive.sdf> <out dir> <pattern> [pattern ...]

Patterns are case-insensitive fnmatch patterns on the file name, e.g. "*.pe". A texture comes out as its pixels only: the
archive keeps a .dds without its 128-byte header (the listed size counts it), so wic_smoke_flipbook.py, which reads the
muzzle smoke's flipbooks through this module, knows their size and layout itself. Plain desktop Python.
"""

import fnmatch
import os
import re
import struct
import sys
import zlib

COMPRESSED = 0x40000000
SIZE_MASK = 0x3FFFFFFF


def read_entries(file):
    """The entries of an .sdf archive as (name, size, offset, stored size). The header's second word points at a
    zlib-compressed table; each name in it ends in a zero and follows five little-endian words: a hash, the size, the
    stored size (its high bits flag compression), the offset and the end. Only a name's first entry is kept."""
    file.seek(4)
    file.seek(struct.unpack("<I", file.read(4))[0])
    _, stored = struct.unpack("<II", file.read(8))
    table = zlib.decompress(file.read(stored & SIZE_MASK))
    entries = {}
    for match in re.finditer(rb"([ -~]{3,})\x00", table):
        if match.start() < 20:
            continue
        _, size, stored, offset, _ = struct.unpack_from("<IIIII", table, match.start() - 20)
        entries.setdefault(match.group(1).decode("latin-1"), (size, offset, stored))
    return [(name, size, offset, stored) for name, (size, offset, stored) in entries.items()]


def read_file(file, offset, stored):
    """An entry's data. A compressed entry is one or more zlib streams back to back."""
    file.seek(offset)
    data = file.read(stored & SIZE_MASK)
    if not stored & COMPRESSED:
        return data
    parts = []
    while data:
        stream = zlib.decompressobj()
        parts.append(stream.decompress(data))
        if not stream.eof:
            raise RuntimeError("truncated zlib stream")
        data = stream.unused_data
    return b"".join(parts)


def read(archive, name):
    """The data of the entry name in the archive at path archive."""
    with open(archive, "rb") as file:
        for entry_name, _, offset, stored in read_entries(file):
            if entry_name.lower() == name.lower():
                return read_file(file, offset, stored)
    raise RuntimeError(f"{name} is not in {archive}")


def matches(name, patterns):
    return any(fnmatch.fnmatch(name.lower(), pattern.lower()) for pattern in patterns)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as file:
        entries = read_entries(file)

        if sys.argv[2] == "--list":
            patterns = sys.argv[3:] or ["*"]
            for name, size, _, _ in entries:
                if matches(name, patterns):
                    print(f"{size:>10}  {name}")
            return

        out_dir, patterns = sys.argv[2], sys.argv[3:]
        if not patterns:
            sys.exit("give at least one pattern to extract")
        for name, _, offset, stored in entries:
            if matches(name, patterns):
                data = read_file(file, offset, stored)
                target = os.path.join(out_dir, name)
                os.makedirs(out_dir, exist_ok=True)
                with open(target, "wb") as out:
                    out.write(data)
                print(f"{name} -> {target}")


if __name__ == "__main__":
    main()
