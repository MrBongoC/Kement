#pragma once
#include <windows.h>

// Kement profiler COM class id. Must match the CORECLR_PROFILER env var (with braces).
// {A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}
static const GUID KementProfilerCLSID = {
    0xa1b2c3d4, 0xe5f6, 0x4a7b, { 0x8c, 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d }
};

#define KEMENT_CLSID_STRING "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
