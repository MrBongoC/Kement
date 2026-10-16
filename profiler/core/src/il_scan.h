#pragma once
#include <windows.h>

// Minimal, conservative IL walker. Return capture rewrites the method's final
// `ret` in place; that is only safe when the last instruction really is a lone
// `ret` at an instruction boundary (so pushing it later shifts nothing that a
// branch targets). This walker confirms that. Anything it can't cleanly parse
// returns false, so we skip capture rather than corrupt IL.
namespace ilscan {

// Operand size for a single-byte opcode. -1 = switch (variable), -2 = 0xFE prefix.
inline int OperandSize1(BYTE op) {
    switch (op) {
        case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: // ldarg.s..stloc.s
        case 0x1F:                                                        // ldc.i4.s
        case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: case 0x30: // short branches
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
        case 0xDE:                                                        // leave.s
            return 1;
        case 0x20: case 0x22:                                             // ldc.i4, ldc.r4
        case 0x27: case 0x28: case 0x29:                                  // jmp, call, calli
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: // long branches
        case 0x3E: case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: // callvirt..castclass
        case 0x75: case 0x79: case 0x7B: case 0x7C: case 0x7D: case 0x7E:
        case 0x7F: case 0x80: case 0x81: case 0x8C: case 0x8D: case 0x8F:
        case 0xA3: case 0xA4: case 0xA5: case 0xC2: case 0xC6: case 0xD0: case 0xDD:
            return 4;
        case 0x21: case 0x23:                                             // ldc.i8, ldc.r8
            return 8;
        case 0x45:                                                        // switch
            return -1;
        case 0xFE:                                                        // two-byte prefix
            return -2;
        default:
            return 0;
    }
}

inline int OperandSize2(BYTE op2) {
    switch (op2) {
        case 0x06: case 0x07: case 0x15: case 0x16: case 0x1C: return 4; // ldftn,ldvirtftn,initobj,constrained,sizeof
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: return 2; // ldarg..stloc
        case 0x12: case 0x19: return 1;                                  // unaligned., no.
        default: return 0;
    }
}

// True if the method body's last instruction is a lone `ret` at a real boundary.
inline bool LastInstrIsLoneRet(const BYTE* code, ULONG codeSize) {
    if (codeSize == 0) return false;
    ULONG i = 0;
    ULONG lastOp = 0xFFFFFFFF;
    ULONG lastOpOffset = 0;
    while (i < codeSize) {
        lastOpOffset = i;
        BYTE op = code[i];
        lastOp = op;
        int sz = OperandSize1(op);
        if (sz == -2) {                       // two-byte opcode
            if (i + 1 >= codeSize) return false;
            BYTE op2 = code[i + 1];
            lastOp = 0xFE00 | op2;
            int sz2 = OperandSize2(op2);
            i += 2 + sz2;
        } else if (sz == -1) {                // switch: 4 + 4*N
            if (i + 5 > codeSize) return false;
            DWORD n = *reinterpret_cast<const DWORD*>(code + i + 1);
            i += 1 + 4 + 4 * n;
        } else {
            i += 1 + sz;
        }
        if (i > codeSize) return false;       // ran past end: malformed for us
    }
    return lastOp == 0x2A && lastOpOffset == codeSize - 1;  // ret is 1 byte, must be last
}

}  // namespace ilscan
