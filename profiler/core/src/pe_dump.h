#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <string>

// Reconstruct an on-disk PE image from a module's in-memory base address.
// The runtime may hold the image either "flat" (byte-identical to the file) or
// "mapped" (sections expanded to their virtual addresses). For flat we copy
// straight through; for mapped we walk the section table back to file offsets.
// Wrapped in SEH so a malformed image fails safe instead of taking the process down.
inline bool ReconstructPE(const BYTE* base, bool flatLayout, std::vector<BYTE>& out) {
    __try {
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

        const BYTE* ntBase = base + dos->e_lfanew;
        if (*reinterpret_cast<const DWORD*>(ntBase) != IMAGE_NT_SIGNATURE) return false;

        const IMAGE_FILE_HEADER* fh =
            reinterpret_cast<const IMAGE_FILE_HEADER*>(ntBase + sizeof(DWORD));
        const BYTE* optBase = reinterpret_cast<const BYTE*>(fh) + sizeof(IMAGE_FILE_HEADER);
        WORD magic = *reinterpret_cast<const WORD*>(optBase);

        DWORD sizeOfHeaders = 0;
        if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            sizeOfHeaders = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(optBase)->SizeOfHeaders;
        else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            sizeOfHeaders = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(optBase)->SizeOfHeaders;
        else
            return false;

        WORD numSections = fh->NumberOfSections;
        const IMAGE_SECTION_HEADER* sections =
            reinterpret_cast<const IMAGE_SECTION_HEADER*>(optBase + fh->SizeOfOptionalHeader);

        // File size = headers plus the furthest section's raw extent.
        DWORD fileSize = sizeOfHeaders;
        for (WORD i = 0; i < numSections; ++i) {
            DWORD end = sections[i].PointerToRawData + sections[i].SizeOfRawData;
            if (end > fileSize) fileSize = end;
        }
        if (fileSize == 0 || fileSize > (256u * 1024u * 1024u)) return false;

        out.assign(fileSize, 0);

        if (flatLayout) {
            memcpy(out.data(), base, fileSize);
        } else {
            memcpy(out.data(), base, sizeOfHeaders);
            for (WORD i = 0; i < numSections; ++i) {
                DWORD raw = sections[i].PointerToRawData;
                DWORD rawSize = sections[i].SizeOfRawData;
                DWORD va = sections[i].VirtualAddress;
                if (raw == 0 || rawSize == 0) continue;
                if (raw + rawSize > fileSize) continue;
                memcpy(out.data() + raw, base + va, rawSize);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Does the reconstructed image carry a CLR (COR20) header, i.e. does it look
// like a loadable managed assembly rather than a native/partial dump? Used to
// flag dump fidelity in the manifest.
inline bool LooksLikeManagedPE(const std::vector<BYTE>& img) {
    if (img.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    const BYTE* base = img.data();
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > img.size()) return false;

    const BYTE* nt = base + dos->e_lfanew;
    if (*reinterpret_cast<const DWORD*>(nt) != IMAGE_NT_SIGNATURE) return false;
    const BYTE* opt = nt + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    WORD magic = *reinterpret_cast<const WORD*>(opt);

    IMAGE_DATA_DIRECTORY com;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        com = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(opt)->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR];
    else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        com = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(opt)->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR];
    else
        return false;

    return com.VirtualAddress != 0 && com.Size != 0;
}

// SHA-256 of a buffer, returned as lowercase hex. Uses Windows CNG (bcrypt).
inline bool Sha256Hex(const BYTE* data, size_t len, std::string& hexOut) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return false;

    bool ok = false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    BYTE digest[32] = {0};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0) == 0 &&
            BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static const char* hexchars = "0123456789abcdef";
            hexOut.clear();
            hexOut.reserve(64);
            for (BYTE b : digest) {
                hexOut.push_back(hexchars[b >> 4]);
                hexOut.push_back(hexchars[b & 0x0F]);
            }
            ok = true;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
