#pragma once
#include <windows.h>
#include "cor.h"
#include "corhdr.h"

// Just enough COR signature parsing to answer: is this a STATIC method whose
// first parameter is a System.String? That is the shape the tripwire's `ldarg.0`
// argument capture requires. Anything the parser can't decode returns false, so
// the sink is conservatively skipped rather than mis-instrumented.
namespace sig {

inline ULONG UncompressData(const BYTE*& p) {
    BYTE b = *p;
    if ((b & 0x80) == 0) { p += 1; return b; }
    if ((b & 0xC0) == 0x80) { ULONG v = ((b & 0x3F) << 8) | p[1]; p += 2; return v; }
    ULONG v = ((b & 0x1F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; p += 4; return v;
}

// Advance p past one type. Best-effort; false on anything unhandled.
inline bool SkipType(const BYTE*& p, const BYTE* end) {
    if (p >= end) return false;
    BYTE t = *p++;
    switch (t) {
        case ELEMENT_TYPE_VOID: case ELEMENT_TYPE_BOOLEAN: case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1: case ELEMENT_TYPE_U1: case ELEMENT_TYPE_I2: case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4: case ELEMENT_TYPE_U4: case ELEMENT_TYPE_I8: case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_R4: case ELEMENT_TYPE_R8: case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_I:  case ELEMENT_TYPE_U:  case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_TYPEDBYREF:
            return true;
        case ELEMENT_TYPE_PTR: case ELEMENT_TYPE_BYREF: case ELEMENT_TYPE_SZARRAY:
        case ELEMENT_TYPE_PINNED:
            return SkipType(p, end);
        case ELEMENT_TYPE_CLASS: case ELEMENT_TYPE_VALUETYPE:
            if (p >= end) return false; UncompressData(p); return true;   // skip token
        case ELEMENT_TYPE_VAR: case ELEMENT_TYPE_MVAR:
            if (p >= end) return false; UncompressData(p); return true;   // skip number
        case ELEMENT_TYPE_CMOD_OPT: case ELEMENT_TYPE_CMOD_REQD:
            if (p >= end) return false; UncompressData(p); return SkipType(p, end);
        case ELEMENT_TYPE_GENERICINST: {
            if (!SkipType(p, end)) return false;
            if (p >= end) return false;
            ULONG n = UncompressData(p);
            for (ULONG i = 0; i < n; ++i) if (!SkipType(p, end)) return false;
            return true;
        }
        default:
            return false;   // ARRAY, FNPTR, etc. -- skip the method
    }
}

inline bool IsStaticStringArg0(PCCOR_SIGNATURE sig, ULONG len) {
    if (sig == nullptr || len < 3) return false;
    const BYTE* p = sig;
    const BYTE* end = sig + len;

    BYTE callconv = *p++;
    if (callconv & IMAGE_CEE_CS_CALLCONV_HASTHIS) return false;   // instance: arg0 is `this`
    if (callconv & IMAGE_CEE_CS_CALLCONV_GENERIC) UncompressData(p);  // skip generic param count

    ULONG paramCount = UncompressData(p);
    if (paramCount < 1) return false;
    if (!SkipType(p, end)) return false;                         // skip return type

    while (p < end && (*p == ELEMENT_TYPE_CMOD_OPT || *p == ELEMENT_TYPE_CMOD_REQD)) {
        ++p; if (p >= end) return false; UncompressData(p);
    }
    return p < end && *p == ELEMENT_TYPE_STRING;
}

}  // namespace sig
