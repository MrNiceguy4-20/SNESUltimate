import os
import re
import struct
import sys
RAW = re.compile(r"^([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})$")
SPLIT = re.compile(r"^([0-9A-Fa-f]{6}):([0-9A-Fa-f]{2})$")
def ram_address(address):
    bank = address >> 16
    offset = address & 0xFFFF
    if bank in (0x7E, 0x7F):
        return True
    if bank < 0x40 or 0x80 <= bank < 0xC0:
        return offset < 0x2000 or 0x6000 <= offset < 0x8000
    return 0x70 <= bank <= 0x7D
def parse_parts(code):
    parts = []
    for piece in code.split("+"):
        piece = piece.strip()
        match = RAW.match(piece) or SPLIT.match(piece)
        if not match:
            return None
        address = int(match.group(1), 16)
        if not ram_address(address):
            return None
        parts.append((address, int(match.group(2), 16)))
    return parts
def parse_file(path):
    descriptions = {}
    codes = {}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = re.match(r'\s*cheat(\d+)_(desc|code)\s*=\s*"([^"]*)"', line)
            if not match:
                continue
            index = int(match.group(1))
            if match.group(2) == "desc":
                descriptions[index] = match.group(3)
            else:
                codes[index] = match.group(3)
    cheats = []
    for index in sorted(codes):
        parts = parse_parts(codes[index])
        if parts:
            cheats.append((descriptions.get(index, "Cheat %d" % index), parts))
    return cheats
def pack_string(text):
    data = text.encode("utf-8")[:65535]
    return struct.pack("<H", len(data)) + data
def main():
    source = sys.argv[1]
    target = sys.argv[2]
    records = []
    total = 0
    for name in sorted(os.listdir(source)):
        if not name.lower().endswith(".cht"):
            continue
        cheats = parse_file(os.path.join(source, name))
        if not cheats:
            continue
        body = struct.pack("<H", len(cheats))
        for description, parts in cheats:
            body += pack_string(description) + struct.pack("<B", len(parts))
            for address, value in parts:
                body += struct.pack("<IB", address, value)
        title = os.path.splitext(name)[0]
        record = pack_string(title) + body
        records.append(struct.pack("<I", len(record)) + record)
        total += len(cheats)
    with open(target, "wb") as handle:
        handle.write(b"SNCH" + struct.pack("<II", 1, len(records)))
        for record in records:
            handle.write(record)
    print("games", len(records), "cheats", total)
main()
