"""Lists or extracts files from the .big archives of an installed C&C Generals / Zero Hour. Used to pull reference art
out of the user's own install into Art/Source/Local (gitignored): EA's art is not ours to commit or ship.

  python Tools/Art/extract_big.py <archive.big> --list [pattern]
  python Tools/Art/extract_big.py <archive.big> <out dir> <pattern> [pattern ...]

Patterns are case-insensitive fnmatch patterns on the archive path, e.g. "art/w3d/nvbtmstr*.w3d". Plain desktop Python.
"""

import fnmatch
import os
import struct
import sys


def read_entries(data):
    """The entries of a BIGF/BIG4 archive as (path, offset, size). Counts and offsets are big-endian; the archive size in
    the header is little-endian and not needed."""
    if data[:4] not in (b"BIGF", b"BIG4"):
        raise RuntimeError("not a .big archive")
    count = struct.unpack_from(">I", data, 8)[0]
    entries = []
    position = 16
    for _ in range(count):
        offset, size = struct.unpack_from(">II", data, position)
        end = data.index(b"\0", position + 8)
        entries.append((data[position + 8:end].decode("latin-1").replace("\\", "/"), offset, size))
        position = end + 1
    return entries


def matches(path, patterns):
    return any(fnmatch.fnmatch(path.lower(), pattern.lower()) for pattern in patterns)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as file:
        data = file.read()
    entries = read_entries(data)

    if sys.argv[2] == "--list":
        patterns = sys.argv[3:] or ["*"]
        for path, _, size in entries:
            if matches(path, patterns):
                print(f"{size:>10}  {path}")
        return

    out_dir, patterns = sys.argv[2], sys.argv[3:]
    if not patterns:
        sys.exit("give at least one pattern to extract")
    for path, offset, size in entries:
        if matches(path, patterns):
            target = os.path.join(out_dir, os.path.basename(path))
            os.makedirs(out_dir, exist_ok=True)
            with open(target, "wb") as file:
                file.write(data[offset:offset + size])
            print(f"{path} -> {target}")


if __name__ == "__main__":
    main()
