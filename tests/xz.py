"""A small xz compressor for tests/untar.sh, so that the tests need no xz program (macOS has none, and Homebrew's is not
to be relied on). It writes the container itself and lets Python's lzma module compress each block as raw LZMA2:
`xz.py [-c] [--check=none|crc32|crc64|sha256] [-T<n>] [--block-size=<bytes>] <file>` writes the .xz stream to stdout.
Several blocks (a block size) are what the xz program makes with --block-size; one block is its default."""

import hashlib
import lzma
import struct
import sys
import zlib

CHECK_IDS = {"none": 0, "crc32": 1, "crc64": 4, "sha256": 10}
CRC64_POLY = 0xC96C5795D7870F42


def crc64_table():
    table = []
    for i in range(256):
        value = i
        for _ in range(8):
            value = (value >> 1) ^ (CRC64_POLY if value & 1 else 0)
        table.append(value)
    return table


CRC64 = crc64_table()


def crc64(data):
    value = 0xFFFFFFFFFFFFFFFF
    for byte in data:
        value = CRC64[(value ^ byte) & 0xFF] ^ (value >> 8)
    return value ^ 0xFFFFFFFFFFFFFFFF


def check_of(kind, data):
    if kind == "none":
        return b""
    if kind == "crc32":
        return struct.pack("<I", zlib.crc32(data))
    if kind == "crc64":
        return struct.pack("<Q", crc64(data))
    return hashlib.sha256(data).digest()


def vli(value):
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def pad4(data):
    return data + b"\0" * (-len(data) % 4)


def block(data, kind):
    # Block header: size byte, flags (one filter, no sizes), LZMA2's id and its one property byte, padding, CRC-32.
    # The dictionary is 8 MiB, which is property byte 22.
    header = bytes([0, 0x00, 0x21, 0x01, 22])
    header = pad4(header + b"\0\0\0\0")[:-4]
    header = bytes([(len(header) + 4) // 4 - 1]) + header[1:]
    header += struct.pack("<I", zlib.crc32(header))
    filters = [{"id": lzma.FILTER_LZMA2, "dict_size": 1 << 23, "preset": 6}]
    packed = lzma.compress(data, format=lzma.FORMAT_RAW, filters=filters)
    check = check_of(kind, data)
    unpadded = len(header) + len(packed) + len(check)
    return pad4(header + packed) + check, unpadded


def compress(data, kind, block_size):
    flags = bytes([0, CHECK_IDS[kind]])
    out = b"\xfd7zXZ\0" + flags + struct.pack("<I", zlib.crc32(flags))
    size = block_size or max(len(data), 1)
    records = b""
    count = 0
    for start in range(0, max(len(data), 1), size):
        chunk = data[start : start + size]
        body, unpadded = block(chunk, kind)
        out += body
        records += vli(unpadded) + vli(len(chunk))
        count += 1
    index = pad4(b"\0" + vli(count) + records)
    index += struct.pack("<I", zlib.crc32(index))
    footer = struct.pack("<I", len(index) // 4 - 1) + flags
    return out + index + struct.pack("<I", zlib.crc32(footer)) + footer + b"YZ"


def main(arguments):
    kind = "crc64"
    block_size = 0
    path = None
    for argument in arguments:
        if argument.startswith("--check="):
            kind = argument[len("--check=") :]
        elif argument.startswith("--block-size="):
            block_size = int(argument[len("--block-size=") :])
        elif argument.startswith("-"):
            continue  # -c and -T<n>
        else:
            path = argument
    with open(path, "rb") as source:
        data = source.read()
    sys.stdout.buffer.write(compress(data, kind, block_size))


main(sys.argv[1:])
