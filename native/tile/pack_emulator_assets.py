"""Bundle emulator resources together so package alignment is paid once."""
import struct
import sys
from pathlib import Path

source, output = map(Path, sys.argv[1:3])
directories = sys.argv[3:]
with output.open("wb") as archive:
    archive.write(b"EFDA")
    for directory in directories:
        for path in sorted((source / directory).rglob("*")):
            if not path.is_file():
                continue
            name = path.relative_to(source).as_posix().encode("utf-8")
            data = path.read_bytes()
            if len(name) >= 512 or len(data) > 8 * 1024 * 1024:
                raise ValueError(f"Invalid emulator asset: {path}")
            archive.write(struct.pack("<II", len(name), len(data)))
            archive.write(name)
            archive.write(data)
    archive.write(struct.pack("<II", 0, 0))
