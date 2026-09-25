#pragma once
#include <windows.h>
#include <vector>
#include "cor.h"
#include "il_scan.h"

// Prepend an arbitrary IL prefix to a method body and return a rebuilt, valid
// method. We always emit a FAT header and shift every exception-clause offset by
// the prefix length. Original relative branches are unaffected because we only
// insert at the very start. minMaxStack is the stack depth the prefix needs.
inline bool RebuildWithPrefix(const BYTE* method, const BYTE* prefix, size_t prefixLen,
                              WORD minMaxStack, std::vector<BYTE>& out) {
    if (method == nullptr) return false;

    const DWORD delta = static_cast<DWORD>(prefixLen);
    BYTE b0 = method[0];
    bool isTiny = (b0 & 0x3) == 0x2;   // CorILMethod_TinyFormat

    DWORD codeSize, localTok = 0, flags = 0;
    WORD  maxStack;
    const BYTE* code;
    const BYTE* ehStart = nullptr;
    bool hasEH = false;

    if (isTiny) {
        codeSize = b0 >> 2;
        maxStack = 8;
        code = method + 1;
    } else if ((b0 & 0x3) == 0x3) {    // CorILMethod_FatFormat
        WORD w0 = *reinterpret_cast<const WORD*>(method);
        flags = w0 & 0x0FFF;
        DWORD hdrDwords = (w0 >> 12) & 0xF;
        maxStack = *reinterpret_cast<const WORD*>(method + 2);
        codeSize = *reinterpret_cast<const DWORD*>(method + 4);
        localTok = *reinterpret_cast<const DWORD*>(method + 8);
        code = method + hdrDwords * 4;
        hasEH = (flags & 0x8) != 0;    // CorILMethod_MoreSects
        if (hasEH) {
            const BYTE* p = code + codeSize;
            size_t pad = (4 - (static_cast<size_t>(p - method) & 3)) & 3;
            ehStart = p + pad;
        }
    } else {
        return false;
    }

    DWORD newCodeSize = codeSize + delta;
    WORD  newMaxStack = maxStack < minMaxStack ? minMaxStack : maxStack;

    out.clear();
    auto put16 = [&](WORD v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
    auto put32 = [&](DWORD v) { for (int i = 0; i < 4; ++i) out.push_back((v >> (8 * i)) & 0xFF); };

    DWORD hdrFlags = 0x3;                                 // fat
    if (hasEH) hdrFlags |= 0x8;                           // MoreSects
    if (!isTiny && (flags & 0x10)) hdrFlags |= 0x10;      // InitLocals
    put16(static_cast<WORD>((hdrFlags & 0x0FFF) | (3 << 12)));
    put16(newMaxStack);
    put32(newCodeSize);
    put32(localTok);

    out.insert(out.end(), prefix, prefix + prefixLen);
    out.insert(out.end(), code, code + codeSize);

    if (!hasEH) return true;

    while (out.size() & 3) out.push_back(0);              // 4-byte align EH table

    const BYTE* sect = ehStart;
    bool more = true;
    while (more) {
        BYTE kind = sect[0];
        bool fat = (kind & 0x40) != 0;                    // Sect_FatFormat
        more = (kind & 0x80) != 0;                        // Sect_MoreSects

        if (fat) {
            DWORD dataSize = (*reinterpret_cast<const DWORD*>(sect)) >> 8;
            DWORD n = (dataSize - 4) / 24;
            out.push_back(kind);
            out.push_back(static_cast<BYTE>(dataSize & 0xFF));
            out.push_back(static_cast<BYTE>((dataSize >> 8) & 0xFF));
            out.push_back(static_cast<BYTE>((dataSize >> 16) & 0xFF));
            const BYTE* cl = sect + 4;
            for (DWORD i = 0; i < n; ++i) {
                DWORD f  = *reinterpret_cast<const DWORD*>(cl + 0);
                DWORD to = *reinterpret_cast<const DWORD*>(cl + 4) + delta;
                DWORD tl = *reinterpret_cast<const DWORD*>(cl + 8);
                DWORD ho = *reinterpret_cast<const DWORD*>(cl + 12) + delta;
                DWORD hl = *reinterpret_cast<const DWORD*>(cl + 16);
                DWORD ct = *reinterpret_cast<const DWORD*>(cl + 20);
                if (f == 0x1) ct += delta;                // FILTER -> offset
                DWORD vals[6] = { f, to, tl, ho, hl, ct };
                for (int j = 0; j < 6; ++j) put32(vals[j]);
                cl += 24;
            }
        } else {
            BYTE dataSize = sect[1];
            DWORD n = (static_cast<DWORD>(dataSize) - 4) / 12;
            out.push_back(kind);
            out.push_back(dataSize);
            out.push_back(0);
            out.push_back(0);
            const BYTE* cl = sect + 4;
            for (DWORD i = 0; i < n; ++i) {
                WORD f  = *reinterpret_cast<const WORD*>(cl + 0);
                WORD to = static_cast<WORD>(*reinterpret_cast<const WORD*>(cl + 2) + delta);
                BYTE tl = *(cl + 4);
                WORD ho = static_cast<WORD>(*reinterpret_cast<const WORD*>(cl + 5) + delta);
                BYTE hl = *(cl + 7);
                DWORD ct = *reinterpret_cast<const DWORD*>(cl + 8);
                if (f == 0x1) ct += delta;
                put16(f); put16(to); out.push_back(tl); put16(ho); out.push_back(hl); put32(ct);
                cl += 12;
            }
        }

        if (more) {
            DWORD secSize = fat ? ((*reinterpret_cast<const DWORD*>(sect)) >> 8) : sect[1];
            const BYTE* next = sect + secSize;
            size_t pad = (4 - (static_cast<size_t>(next - method) & 3)) & 3;
            sect = next + pad;
        }
    }
    return true;
}

// Return capture: rewrite a method's final `ret` in place to
// `dup; ldstr <label>; call TraceReturn(object,string); ret`, so the returned
// object is logged. Safe only for single-trailing-ret, no-EH methods (validated
// here); returns false otherwise so the caller skips capture. Reference return
// only (the value is passed as object with no boxing).
inline bool BuildReturnCaptureBody(const BYTE* method, mdString labelUs,
                                   mdToken traceReturnTok, std::vector<BYTE>& out) {
    BYTE b0 = method[0];
    bool isTiny = (b0 & 0x3) == 0x2;
    DWORD codeSize, localTok = 0, flags = 0;
    WORD maxStack;
    const BYTE* code;
    if (isTiny) { codeSize = b0 >> 2; maxStack = 8; code = method + 1; }
    else if ((b0 & 0x3) == 0x3) {
        WORD w0 = *reinterpret_cast<const WORD*>(method);
        flags = w0 & 0x0FFF;
        DWORD hdrDwords = (w0 >> 12) & 0xF;
        maxStack = *reinterpret_cast<const WORD*>(method + 2);
        codeSize = *reinterpret_cast<const DWORD*>(method + 4);
        localTok = *reinterpret_cast<const DWORD*>(method + 8);
        code = method + hdrDwords * 4;
        if (flags & 0x8) return false;   // has EH: not supported by in-place capture
    } else return false;

    if (codeSize < 1 || !ilscan::LastInstrIsLoneRet(code, codeSize)) return false;

    out.clear();
    DWORD newCode = codeSize + 11;
    WORD  newMax = static_cast<WORD>(maxStack + 2);
    DWORD hdrFlags = 0x3 | (flags & 0x10);   // fat + preserve InitLocals
    auto put16 = [&](WORD v) { out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF); };
    auto put32 = [&](DWORD v) { for (int i = 0; i < 4; ++i) out.push_back((v >> (8 * i)) & 0xFF); };
    put16(static_cast<WORD>((hdrFlags & 0x0FFF) | (3 << 12)));
    put16(newMax);
    put32(newCode);
    put32(localTok);
    out.insert(out.end(), code, code + (codeSize - 1));   // original minus final ret
    out.push_back(0x25);                                  // dup
    out.push_back(0x72); for (int i = 0; i < 4; ++i) out.push_back(static_cast<BYTE>((labelUs >> (8 * i)) & 0xFF));       // ldstr
    out.push_back(0x28); for (int i = 0; i < 4; ++i) out.push_back(static_cast<BYTE>((traceReturnTok >> (8 * i)) & 0xFF)); // call
    out.push_back(0x2A);                                  // ret
    return true;
}

// Whole-body replace: `ldnull; ret` (for reference-returning methods).
inline void BuildReturnNullBody(std::vector<BYTE>& out) {
    out = { static_cast<BYTE>((2 << 2) | 0x2), 0x14, 0x2A };  // tiny hdr, ldnull, ret
}

// Whole-body replace: `ldstr <msg>; newobj <exnCtor>; throw`.
inline void BuildThrowBody(mdString msgUs, mdToken exnCtor, std::vector<BYTE>& out) {
    out.clear();
    out.push_back(static_cast<BYTE>((11 << 2) | 0x2));   // tiny header, codeSize 11
    out.push_back(0x72); for (int i = 0; i < 4; ++i) out.push_back(static_cast<BYTE>((msgUs >> (8 * i)) & 0xFF));   // ldstr
    out.push_back(0x73); for (int i = 0; i < 4; ++i) out.push_back(static_cast<BYTE>((exnCtor >> (8 * i)) & 0xFF)); // newobj
    out.push_back(0x7A);  // throw
}

// Startup path: prepend `ldc.i8 <sinkId>; ldarg.0; call <hitRef>` -- a call to
// the managed probe Kement.Probes.Sink::Hit(long, string).
inline bool BuildSinkInstrumentedIL(const BYTE* method, INT64 sinkId,
                                    mdToken hitRef, std::vector<BYTE>& out) {
    BYTE prefix[15];
    size_t k = 0;
    prefix[k++] = 0x21;                                   // ldc.i8
    for (int i = 0; i < 8; ++i) prefix[k++] = static_cast<BYTE>((sinkId >> (8 * i)) & 0xFF);
    prefix[k++] = 0x02;                                   // ldarg.0
    prefix[k++] = 0x28;                                   // call
    for (int i = 0; i < 4; ++i) prefix[k++] = static_cast<BYTE>((hitRef >> (8 * i)) & 0xFF);
    return RebuildWithPrefix(method, prefix, 15, 2, out);
}

// Attach path: log via Internal.Console.WriteLine (same-assembly, no probe
// needed), and optionally block. Tokens are all CoreLib methoddefs/user strings.
//   log:   ldstr <logUs>; ldarg.0; call <concat>; call <writeLine>
//   block: ldarg.0; ldstr <denyUs>; callvirt <contains>; brfalse.s +11;
//          ldstr <msgUs>; newobj <exnCtor>; throw
inline bool BuildSinkAttachIL(const BYTE* method, mdString logUs, mdToken concatToken,
                              mdToken writeLineToken, bool doBlock, mdString denyUs,
                              mdToken containsToken, mdToken exnCtorToken, mdString msgUs,
                              std::vector<BYTE>& out) {
    std::vector<BYTE> prefix;
    auto emit8  = [&](BYTE b) { prefix.push_back(b); };
    auto emitTk = [&](mdToken t) { for (int i = 0; i < 4; ++i) prefix.push_back(static_cast<BYTE>((t >> (8 * i)) & 0xFF)); };

    emit8(0x72); emitTk(logUs);        // ldstr <logUs>
    emit8(0x02);                       // ldarg.0
    emit8(0x28); emitTk(concatToken);  // call String::Concat(string,string)
    emit8(0x28); emitTk(writeLineToken); // call Internal.Console::WriteLine(string)

    if (doBlock) {
        emit8(0x02);                       // ldarg.0
        emit8(0x72); emitTk(denyUs);       // ldstr <denyUs>
        emit8(0x6F); emitTk(containsToken);// callvirt String::Contains(string)
        emit8(0x2C); emit8(0x0B);          // brfalse.s +11 (skip the throw block)
        emit8(0x72); emitTk(msgUs);        // ldstr <msgUs>
        emit8(0x73); emitTk(exnCtorToken); // newobj UnauthorizedAccessException(string)
        emit8(0x7A);                       // throw
    }
    return RebuildWithPrefix(method, prefix.data(), prefix.size(), 2, out);
}
