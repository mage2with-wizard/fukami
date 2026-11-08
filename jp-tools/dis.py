# dis.py <elf> <start-hex> <end-hex>: MIPS (R5900 subset) disassembly of a PS2 ELF range
import sys, struct, capstone
elf = open(sys.argv[1], 'rb').read()
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
phoff, = struct.unpack_from('<I', elf, 0x1c); phnum, = struct.unpack_from('<H', elf, 0x2c)
for i in range(phnum):
    t, off, va, pa, fsz, msz = struct.unpack_from('<6I', elf, phoff + i * 32)
    if t == 1 and va <= start < va + fsz:
        data = elf[off + start - va: off + end - va]; break
md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS64 + capstone.CS_MODE_LITTLE_ENDIAN)
for ins in md.disasm(data, start):
    print(f"{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")
