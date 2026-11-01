#!/usr/bin/env python3
"""Gera ISO9660 propria com SYSTEM.CNF e a demo PS-X EXE do Aurora."""
import argparse
import math
import re
from pathlib import Path
import struct


def both16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)


def both32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)


def record(name, extent, size, directory=False):
    result = bytearray(33 + len(name) + (len(name) % 2 == 0))
    result[0] = len(result)
    result[2:10] = both32(extent)
    result[10:18] = both32(size)
    result[18:25] = bytes((126, 11, 2, 0, 0, 0, 0))
    result[25] = 2 if directory else 0
    result[28:32] = both16(1)
    result[32] = len(name)
    result[33:33 + len(name)] = name
    return result


def generate(demo, volume_id="AURORA_DEMO", executable_name="DEMO.PSX"):
    if not re.fullmatch(r"[A-Z0-9_]{1,32}", volume_id):
        raise ValueError("Volume ISO deve usar de 1 a 32 letras maiusculas, numeros ou underscore.")
    if not re.fullmatch(r"[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}", executable_name):
        raise ValueError("Executavel ISO deve ter nome ASCII maiusculo 8.3.")
    if not (0x800 <= len(demo) <= 2 * 1024 * 1024 + 0x800) or demo[:8] != b"PS-X EXE":
        raise ValueError("Entrada deve ser um PS-X EXE proprio, valido e limitado a 2 MiB de RAM.")
    payload_size = struct.unpack_from("<I", demo, 0x1C)[0]
    if payload_size == 0 or payload_size + 0x800 > len(demo):
        raise ValueError("Payload PS-X EXE truncado ou vazio.")
    block = 2048
    system = f"BOOT = cdrom:\\{executable_name};1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n".encode("ascii")
    exe_extent = 22
    blocks = max(32, exe_extent + math.ceil(len(demo) / block) + 8)
    image = bytearray(blocks * block)
    pvd = bytearray(block)
    pvd[:7] = b"\x01CD001\x01"
    pvd[8:40] = b"PLAYSTATION".ljust(32, b" ")
    pvd[40:72] = volume_id.encode("ascii").ljust(32, b" ")
    pvd[80:88] = both32(blocks)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(block)
    pvd[132:140] = both32(10)
    struct.pack_into("<I", pvd, 140, 18)
    struct.pack_into(">I", pvd, 148, 19)
    pvd[156:190] = record(b"\0", 20, block, True)
    pvd[881] = 1
    image[16 * block:17 * block] = pvd
    image[17 * block:17 * block + 7] = b"\xffCD001\x01"
    image[18 * block:18 * block + 10] = bytes((1, 0)) + struct.pack("<IH", 20, 1) + b"\0\0"
    image[19 * block:19 * block + 10] = bytes((1, 0)) + struct.pack(">IH", 20, 1) + b"\0\0"
    directory = b"".join((
        record(b"\0", 20, block, True),
        record(b"\1", 20, block, True),
        record(b"SYSTEM.CNF;1", 21, len(system)),
        record(f"{executable_name};1".encode("ascii"), exe_extent, len(demo)),
    ))
    image[20 * block:20 * block + len(directory)] = directory
    image[21 * block:21 * block + len(system)] = system
    image[exe_extent * block:exe_extent * block + len(demo)] = demo
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("demo", type=Path, help="PS-X EXE produzido por aurora-cli --make-demo")
    parser.add_argument("output", type=Path, help="ISO9660 de demonstracao a gerar")
    parser.add_argument("--volume", default="AURORA_DEMO", help="Identificador do volume ISO")
    parser.add_argument("--boot", default="DEMO.PSX", help="Nome 8.3 do PS-X EXE na ISO")
    args = parser.parse_args()
    if args.demo.resolve() == args.output.resolve():
        parser.error("A entrada e a saida precisam ser arquivos diferentes.")
    if args.demo.stat().st_size > 2 * 1024 * 1024 + 0x800:
        parser.error("Entrada maior que o limite de PS-X EXE.")
    image = generate(args.demo.read_bytes(), args.volume, args.boot)
    with args.output.open("xb") as output:
        output.write(image)
    print(f"ISO propria criada: {args.output} ({len(image)} bytes). Nao inclui BIOS ou jogo comercial.")


if __name__ == "__main__":
    main()
