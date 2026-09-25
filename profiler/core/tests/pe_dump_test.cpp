// Unit test for the PE reconstruction used by the unpacker. The CLR won't easily
// hand us a MAPPED in-memory module, so we synthesize one from a real on-disk DLL
// (expand sections to their virtual addresses) and check ReconstructPE(mapped)
// reproduces the original file byte-for-byte. Also checks the flat pass-through.
#include "pe_dump.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

static std::vector<BYTE> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<BYTE>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool SameHash(const std::vector<BYTE>& a, const std::vector<BYTE>& b) {
    std::string ha, hb;
    Sha256Hex(a.data(), a.size(), ha);
    Sha256Hex(b.data(), b.size(), hb);
    printf("    got=%s\n    want=%s\n", ha.c_str(), hb.c_str());
    return ha == hb;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: pe_dump_test <file.dll>\n"); return 64; }

    std::vector<BYTE> flat = ReadFile(argv[1]);
    if (flat.empty()) { printf("could not read %s\n", argv[1]); return 1; }
    printf("loaded %s (%zu bytes)\n", argv[1], flat.size());

    // Parse enough of the flat file to build a mapped image.
    const BYTE* base = flat.data();
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const BYTE* nt = base + dos->e_lfanew;
    auto fh = reinterpret_cast<const IMAGE_FILE_HEADER*>(nt + sizeof(DWORD));
    const BYTE* opt = reinterpret_cast<const BYTE*>(fh) + sizeof(IMAGE_FILE_HEADER);
    WORD magic = *reinterpret_cast<const WORD*>(opt);
    DWORD sizeOfHeaders, sizeOfImage;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        auto o = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(opt);
        sizeOfHeaders = o->SizeOfHeaders; sizeOfImage = o->SizeOfImage;
    } else {
        auto o = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(opt);
        sizeOfHeaders = o->SizeOfHeaders; sizeOfImage = o->SizeOfImage;
    }
    auto sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(opt + fh->SizeOfOptionalHeader);

    std::vector<BYTE> mapped(sizeOfImage, 0);
    memcpy(mapped.data(), base, sizeOfHeaders);
    for (WORD i = 0; i < fh->NumberOfSections; ++i) {
        DWORD raw = sections[i].PointerToRawData, rawSize = sections[i].SizeOfRawData, va = sections[i].VirtualAddress;
        if (raw && rawSize && va + rawSize <= sizeOfImage && raw + rawSize <= flat.size())
            memcpy(mapped.data() + va, base + raw, rawSize);
    }
    printf("built synthetic mapped image (%u bytes, %u sections)\n", sizeOfImage, fh->NumberOfSections);

    int failures = 0;

    printf("[test] flat reconstruction\n");
    std::vector<BYTE> outFlat;
    if (!ReconstructPE(flat.data(), true, outFlat) || !SameHash(outFlat, flat)) {
        printf("    FAIL\n"); ++failures;
    } else printf("    PASS\n");

    printf("[test] mapped reconstruction\n");
    std::vector<BYTE> outMapped;
    if (!ReconstructPE(mapped.data(), false, outMapped) || !SameHash(outMapped, flat)) {
        printf("    FAIL\n"); ++failures;
    } else printf("    PASS\n");

    printf("[test] fidelity check (managed)\n");
    if (!LooksLikeManagedPE(outMapped)) { printf("    FAIL (expected managed)\n"); ++failures; }
    else printf("    PASS\n");

    printf(failures == 0 ? "RESULT: PASS\n" : "RESULT: FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 2;
}
