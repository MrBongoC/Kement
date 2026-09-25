#include "KementProfiler.h"
#include "logger.h"
#include "pe_dump.h"
#include "il_rewrite.h"
#include "sig_parse.h"
#include <cwchar>
#include <fstream>
#include <vector>

// Tripwire sink set. Every overload of these names whose signature is
// static-with-a-string-at-arg0 gets instrumented (so `ldarg.0` captures the
// path/command safely); other overloads are skipped.
namespace {
struct SinkDef { const wchar_t* type; const wchar_t* method; INT64 id; };
const SinkDef kSinks[] = {
    { L"System.IO.File",              L"ReadAllText",   1 },
    { L"System.IO.File",              L"ReadAllBytes",  2 },
    { L"System.IO.File",              L"WriteAllText",  3 },
    { L"System.IO.File",              L"Delete",        4 },
    { L"System.IO.File",              L"AppendAllText", 6 },
    { L"System.Diagnostics.Process",  L"Start",         5 },
};
}

namespace {
std::wstring GetEnvW(const wchar_t* name) {
    wchar_t buf[1024] = {0};
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    if (n == 0 || n >= 1024) return std::wstring();
    return std::wstring(buf, n);
}
bool ContainsNoCase(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring h = hay, n = needle;
    for (auto& c : h) c = towlower(c);
    for (auto& c : n) c = towlower(c);
    return h.find(n) != std::wstring::npos;
}
}

HRESULT KementProfiler::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == __uuidof(ICorProfilerCallback) ||
        riid == __uuidof(ICorProfilerCallback2) ||
        riid == __uuidof(ICorProfilerCallback3) ||
        riid == __uuidof(ICorProfilerCallback4)) {
        *ppv = static_cast<ICorProfilerCallback4*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG KementProfiler::AddRef()  { return ++refCount_; }
ULONG KementProfiler::Release() {
    ULONG c = --refCount_;
    if (c == 0) delete this;
    return c;
}

void KementProfiler::ApplyConfigString(const std::wstring& settings) {
    // newline-separated KEY=VALUE pairs (client data on attach).
    size_t pos = 0;
    while (pos < settings.size()) {
        size_t nl = settings.find(L'\n', pos);
        std::wstring line = settings.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? settings.size() : nl + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        if (key == L"LOG")               ; // consumed before Logger init
        else if (key == L"DUMP")         dumpDir_ = val;
        else if (key == L"TARGET")       targetAssembly_ = val;
        else if (key == L"REJIT_TYPE")   rejitType_ = val;
        else if (key == L"REJIT_METHOD") rejitMethod_ = val;
        else if (key == L"REJIT_RETVAL") rejitReturnValue_ = static_cast<INT32>(wcstol(val.c_str(), nullptr, 10));
        else if (key == L"TRIPWIRE")     tripwireEnabled_ = (val == L"1");
        else if (key == L"MODE")         attachMode_ = val;
        else if (key == L"DENY")         attachDeny_ = val;
        else if (key == L"HOOKS")        LoadHooks(val);
    }
}

HRESULT KementProfiler::CommonInit(IUnknown* infoUnk, bool onAttach) {
    HRESULT hr = infoUnk->QueryInterface(
        __uuidof(ICorProfilerInfo8), reinterpret_cast<void**>(&info_));
    if (FAILED(hr) || info_ == nullptr) {
        KLOG("FATAL: ICorProfilerInfo8 not available (hr=0x%08x)", hr);
        return E_FAIL;
    }
    // Optional: ICorProfilerInfo10 enables ReJIT-on-attach (RequestReJITWithInliners).
    infoUnk->QueryInterface(__uuidof(ICorProfilerInfo10), reinterpret_cast<void**>(&info10_));

    if (targetAssembly_.empty()) targetAssembly_ = L"TestTarget";
    rejitEnabled_ = !rejitType_.empty() && !rejitMethod_.empty();
    unpackEnabled_ = !dumpDir_.empty();
    if (unpackEnabled_) CreateDirectoryW(dumpDir_.c_str(), nullptr);

    // Try to enable ReJIT even on attach (InitializeForAttach is initialization,
    // and .NET Core relaxed the startup-only rule for ReJIT-on-attach). If the
    // runtime still rejects the immutable flag on attach, fall back to the
    // attach-safe subset (observe-only).
    DWORD mask = COR_PRF_MONITOR_JIT_COMPILATION | COR_PRF_MONITOR_MODULE_LOADS | COR_PRF_ENABLE_REJIT;
    hr = info_->SetEventMask(mask);
    if (FAILED(hr) && onAttach) {
        KLOG("SetEventMask with ReJIT rejected on attach (hr=0x%08x); retrying observe-only", hr);
        rejitOnAttachOk_ = false;
        mask = COR_PRF_MONITOR_JIT_COMPILATION | COR_PRF_MONITOR_MODULE_LOADS;
        hr = info_->SetEventMask(mask);
    } else if (onAttach) {
        rejitOnAttachOk_ = true;
    }
    if (FAILED(hr)) {
        KLOG("FATAL: SetEventMask failed (hr=0x%08x, onAttach=%d)", hr, onAttach ? 1 : 0);
        return hr;
    }

    KLOG("Core %s. target-assembly=\"%ls\"",
         onAttach ? "attached to running process" : "loaded at startup",
         targetAssembly_.c_str());
    if (onAttach && rejitEnabled_)
        KLOG("Note: ReJIT is startup-only; ignoring ReJIT config on attach (M4 adds attach ReJIT)");
    else if (rejitEnabled_)
        KLOG("ReJIT armed: %ls::%ls -> return %d",
             rejitType_.c_str(), rejitMethod_.c_str(), rejitReturnValue_);
    if (unpackEnabled_)
        KLOG("Unpacker armed: dumping in-memory modules to %ls", dumpDir_.c_str());
    if (tripwireEnabled_) {
        if (onAttach && !info10_)
            KLOG("Tripwire: ReJIT-on-attach needs ICorProfilerInfo10 (unavailable); tripwire inactive");
        else if (onAttach)
            KLOG("Tripwire armed on attach: instrumenting file sinks via Internal.Console (monitor mode)");
        else
            KLOG("Tripwire armed: instrumenting %zu sink names (monitor/alert/block)", sizeof(kSinks) / sizeof(kSinks[0]));
    }
    if (hooksEnabled_)
        KLOG("Hook engine armed: %zu hook(s)", hooks_.size());
    return S_OK;
}

HRESULT KementProfiler::Initialize(IUnknown* pICorProfilerInfoUnk) {
    Logger::Get().Init();  // reads KEMENT_LOG from env

    targetAssembly_ = GetEnvW(L"KEMENT_TARGET_ASSEMBLY");
    rejitType_   = GetEnvW(L"KEMENT_REJIT_TYPE");
    rejitMethod_ = GetEnvW(L"KEMENT_REJIT_METHOD");
    std::wstring rv = GetEnvW(L"KEMENT_REJIT_RETVAL");
    if (!rv.empty()) rejitReturnValue_ = static_cast<INT32>(wcstol(rv.c_str(), nullptr, 10));
    dumpDir_ = GetEnvW(L"KEMENT_DUMP_DIR");
    tripwireEnabled_ = GetEnvW(L"KEMENT_TRIPWIRE") == L"1";
    std::wstring hooksPath = GetEnvW(L"KEMENT_HOOKS");
    if (!hooksPath.empty()) LoadHooks(hooksPath);

    HRESULT hr = CommonInit(pICorProfilerInfoUnk, false);

    // Anti-analysis (opt-in): the CLR has already read the profiler env vars, so
    // scrub them now. A sample that checks Environment.GetEnvironmentVariable
    // ("CORECLR_PROFILER") to detect instrumentation will see nothing.
    if (SUCCEEDED(hr) && GetEnvW(L"KEMENT_HIDE") == L"1") {
        SetEnvironmentVariableW(L"CORECLR_ENABLE_PROFILING", nullptr);
        SetEnvironmentVariableW(L"CORECLR_PROFILER", nullptr);
        SetEnvironmentVariableW(L"CORECLR_PROFILER_PATH", nullptr);
        SetEnvironmentVariableW(L"CORECLR_PROFILER_PATH_64", nullptr);
        KLOG("Anti-analysis: scrubbed CORECLR_* profiler env vars from the target");
    }
    return hr;
}

HRESULT KementProfiler::InitializeForAttach(IUnknown* pCorProfilerInfoUnk,
                                            void* pvClientData, UINT cbClientData) {
    attached_ = true;

    // Client data is a UTF-16LE string of KEY=VALUE lines from the controller.
    std::wstring settings;
    if (pvClientData != nullptr && cbClientData >= sizeof(wchar_t)) {
        settings.assign(reinterpret_cast<const wchar_t*>(pvClientData),
                        cbClientData / sizeof(wchar_t));
    }

    // Pull LOG out first so the logger can open before we emit anything.
    std::wstring logPath;
    size_t lp = settings.find(L"LOG=");
    if (lp != std::wstring::npos) {
        size_t start = lp + 4;
        size_t nl = settings.find(L'\n', start);
        logPath = settings.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
        while (!logPath.empty() && (logPath.back() == L'\r' || logPath.back() == L' ')) logPath.pop_back();
    }
    Logger::Get().Init(logPath.empty() ? nullptr : logPath.c_str());

    ApplyConfigString(settings);
    return CommonInit(pCorProfilerInfoUnk, true);
}

HRESULT KementProfiler::ProfilerAttachComplete() {
    // Modules loaded before we attached never fire ModuleLoadFinished, so sweep
    // them now (this is where CoreLib's already-loaded file sinks get hooked).
    if ((tripwireEnabled_ || hooksEnabled_) && info10_) {
        KLOG("Attach complete: sweeping already-loaded modules");
        InstrumentLoadedModules();
    }
    return S_OK;
}

HRESULT KementProfiler::Shutdown() {
    KLOG("Shutdown. total methods JITted=%lu, target-assembly JITted=%lu",
         jitCount_.load(), targetJitCount_.load());
    return S_OK;
}

HRESULT KementProfiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) {
    if (FAILED(hrStatus)) return S_OK;

    WCHAR name[1024] = {0};
    ULONG cch = 0;
    LPCBYTE base = nullptr;
    AssemblyID asmId = 0;
    DWORD flags = 0;
    if (FAILED(info_->GetModuleInfo2(moduleId, &base, 1024, &cch, name, &asmId, &flags)))
        return S_OK;

    std::wstring modName(name);

    // Unpacker: a module the runtime materialized in memory (not backed by a
    // disk file, not reflection-emit, not NGEN) is a candidate hidden payload.
    if (unpackEnabled_ && base != nullptr &&
        (flags & COR_PRF_MODULE_DISK) == 0 &&
        (flags & COR_PRF_MODULE_DYNAMIC) == 0 &&
        (flags & COR_PRF_MODULE_NGEN) == 0) {
        DumpInMemoryModule(moduleId, modName, base, flags);
    }

    // Tripwire: instrument any configured sink this module happens to declare.
    if (tripwireEnabled_) {
        InstrumentSinks(moduleId);
    }
    // Generic hooks.
    if (hooksEnabled_) {
        InstrumentHooks(moduleId);
    }

    if (!ContainsNoCase(modName, targetAssembly_)) return S_OK;

    KLOG("Target module loaded: %ls", modName.c_str());
    if (rejitEnabled_ && !rejitRequested_) {
        mdMethodDef token = mdTokenNil;
        if (SUCCEEDED(ResolveMethodToken(moduleId, &token)) && token != mdTokenNil) {
            ModuleID mods[1] = { moduleId };
            mdMethodDef toks[1] = { token };
            HRESULT hr = info_->RequestReJIT(1, mods, toks);
            if (SUCCEEDED(hr)) {
                rejitRequested_ = true;
                demoToken_ = token;
                KLOG("RequestReJIT ok for %ls::%ls (token=0x%08x)",
                     rejitType_.c_str(), rejitMethod_.c_str(), token);
            } else {
                KLOG("RequestReJIT FAILED (hr=0x%08x)", hr);
            }
        } else {
            KLOG("Could not resolve %ls::%ls in %ls",
                 rejitType_.c_str(), rejitMethod_.c_str(), modName.c_str());
        }
    }
    return S_OK;
}

HRESULT KementProfiler::ResolveMethodToken(ModuleID moduleId, mdMethodDef* pToken) {
    IMetaDataImport* import = nullptr;
    HRESULT hr = info_->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport,
                                          reinterpret_cast<IUnknown**>(&import));
    if (FAILED(hr) || import == nullptr) return hr;

    mdTypeDef td = mdTypeDefNil;
    hr = import->FindTypeDefByName(rejitType_.c_str(), mdTokenNil, &td);
    if (SUCCEEDED(hr)) {
        hr = import->FindMethod(td, rejitMethod_.c_str(), nullptr, 0, pToken);
    }
    import->Release();
    return hr;
}

mdMemberRef KementProfiler::EnsureHitRef(ModuleID moduleId) {
    {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        auto it = hitRefByModule_.find(moduleId);
        if (it != hitRefByModule_.end()) return it->second;
    }

    IMetaDataEmit2* emit = nullptr;
    IMetaDataAssemblyEmit* asmEmit = nullptr;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit2,
                                        reinterpret_cast<IUnknown**>(&emit))) || !emit)
        return mdTokenNil;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataAssemblyEmit,
                                        reinterpret_cast<IUnknown**>(&asmEmit))) || !asmEmit) {
        emit->Release();
        return mdTokenNil;
    }

    mdMemberRef hitRef = mdTokenNil;
    ASSEMBLYMETADATA amd = {0};
    amd.usMajorVersion = 1;   // Kement.Probes, Version=1.0.0.0, no public key
    mdAssemblyRef asmRef = mdTokenNil;
    HRESULT hr = asmEmit->DefineAssemblyRef(nullptr, 0, L"Kement.Probes", &amd,
                                            nullptr, 0, 0, &asmRef);
    if (SUCCEEDED(hr)) {
        mdTypeRef typeRef = mdTokenNil;
        hr = emit->DefineTypeRefByName(asmRef, L"Kement.Probes.Sink", &typeRef);
        if (SUCCEEDED(hr)) {
            // void Hit(int64 sinkId, string arg)
            const BYTE sig[] = { 0x00, 0x02, ELEMENT_TYPE_VOID, ELEMENT_TYPE_I8, ELEMENT_TYPE_STRING };
            hr = emit->DefineMemberRef(typeRef, L"Hit", sig, sizeof(sig), &hitRef);
        }
    }
    emit->Release();
    asmEmit->Release();

    if (FAILED(hr)) {
        KLOG("Tripwire: metadata emit failed for module %llu (hr=0x%08x)",
             static_cast<unsigned long long>(moduleId), hr);
        return mdTokenNil;
    }
    std::lock_guard<std::mutex> lock(sinkMutex_);
    hitRefByModule_[moduleId] = hitRef;
    return hitRef;
}

bool KementProfiler::EnsureConsoleRefs(ModuleID moduleId, ConsoleRefs* out) {
    {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        auto it = consoleRefsByModule_.find(moduleId);
        if (it != consoleRefsByModule_.end()) { *out = it->second; return out->writeLine != mdTokenNil && out->concat != mdTokenNil; }
    }
    IMetaDataImport* import = nullptr;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport,
                                        reinterpret_cast<IUnknown**>(&import))) || !import)
        return false;

    ConsoleRefs r{ mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil };
    mdTypeDef consoleTd = mdTypeDefNil, stringTd = mdTypeDefNil, exnTd = mdTypeDefNil;
    const BYTE wlSig[]       = { 0x00, 0x01, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING };
    const BYTE ccSig[]       = { 0x00, 0x02, ELEMENT_TYPE_STRING, ELEMENT_TYPE_STRING, ELEMENT_TYPE_STRING };
    const BYTE containsSig[] = { IMAGE_CEE_CS_CALLCONV_HASTHIS, 0x01, ELEMENT_TYPE_BOOLEAN, ELEMENT_TYPE_STRING };
    const BYTE ctorSig[]     = { IMAGE_CEE_CS_CALLCONV_HASTHIS, 0x01, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING };
    if (SUCCEEDED(import->FindTypeDefByName(L"Internal.Console", mdTokenNil, &consoleTd)))
        import->FindMethod(consoleTd, L"WriteLine", wlSig, sizeof(wlSig), &r.writeLine);
    if (SUCCEEDED(import->FindTypeDefByName(L"System.String", mdTokenNil, &stringTd))) {
        import->FindMethod(stringTd, L"Concat", ccSig, sizeof(ccSig), &r.concat);
        import->FindMethod(stringTd, L"Contains", containsSig, sizeof(containsSig), &r.contains);
    }
    if (SUCCEEDED(import->FindTypeDefByName(L"System.UnauthorizedAccessException", mdTokenNil, &exnTd)))
        import->FindMethod(exnTd, L".ctor", ctorSig, sizeof(ctorSig), &r.exnCtor);
    import->Release();

    // Block-mode user strings (deny term + message) live in this module's #US heap.
    if (attachMode_ == L"block" && !attachDeny_.empty()) {
        IMetaDataEmit2* emit = nullptr;
        if (SUCCEEDED(info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit2,
                                               reinterpret_cast<IUnknown**>(&emit))) && emit) {
            emit->DefineUserString(attachDeny_.c_str(), static_cast<ULONG>(attachDeny_.size()), &r.denyUs);
            std::wstring msg = L"[kement] blocked by tripwire: " + attachDeny_;
            emit->DefineUserString(msg.c_str(), static_cast<ULONG>(msg.size()), &r.msgUs);
            emit->Release();
        }
    }

    std::lock_guard<std::mutex> lock(sinkMutex_);
    consoleRefsByModule_[moduleId] = r;
    *out = r;
    return r.writeLine != mdTokenNil && r.concat != mdTokenNil;
}

void KementProfiler::InstrumentSinks(ModuleID moduleId) {
    IMetaDataImport* import = nullptr;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport,
                                        reinterpret_cast<IUnknown**>(&import))) || !import)
        return;

    // On attach we can't load our probe assembly, so we log via CoreLib's own
    // Internal.Console (same-assembly call). That only works in the module that
    // declares it (CoreLib, where the file sinks live).
    ConsoleRefs refs{};
    bool consoleReady = attached_ && EnsureConsoleRefs(moduleId, &refs);

    IMetaDataEmit2* emit = nullptr;  // only for DefineUserString on attach
    if (attached_)
        info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit2,
                                 reinterpret_cast<IUnknown**>(&emit));

    mdMemberRef hitRef = mdTokenNil;
    for (const auto& s : kSinks) {
        mdTypeDef td = mdTypeDefNil;
        if (FAILED(import->FindTypeDefByName(s.type, mdTokenNil, &td))) continue;

        // Instrument every overload that is static with a string at arg0.
        HCORENUM en = nullptr;
        mdMethodDef methods[64];
        ULONG got = 0;
        while (SUCCEEDED(import->EnumMethodsWithName(&en, td, s.method, methods, 64, &got)) && got > 0) {
            for (ULONG i = 0; i < got; ++i) {
                DWORD attr = 0; PCCOR_SIGNATURE psig = nullptr; ULONG csig = 0;
                if (FAILED(import->GetMethodProps(methods[i], nullptr, nullptr, 0, nullptr,
                                                  &attr, &psig, &csig, nullptr, nullptr)))
                    continue;
                if (!(attr & mdStatic)) continue;
                if (!sig::IsStaticStringArg0(psig, csig)) continue;

                mdString us = mdTokenNil;
                if (attached_) {
                    if (!consoleReady || !emit) continue;  // can't log here on attach
                    wchar_t prefix[128];
                    swprintf(prefix, 128, L"[kement-sink] %ls -> ", s.method);
                    emit->DefineUserString(prefix, static_cast<ULONG>(wcslen(prefix)), &us);
                } else {
                    if (hitRef == mdTokenNil) {
                        hitRef = EnsureHitRef(moduleId);
                        if (hitRef == mdTokenNil) break;
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(sinkMutex_);
                    sinkMethods_[{moduleId, methods[i]}] = { s.id, us };
                }
                ModuleID mods[1] = { moduleId };
                mdMethodDef toks[1] = { methods[i] };
                HRESULT hr = attached_ && info10_
                    ? info10_->RequestReJITWithInliners(COR_PRF_REJIT_BLOCK_INLINING, 1, mods, toks)
                    : info_->RequestReJIT(1, mods, toks);
                KLOG("Tripwire: hook %ls::%ls (token=0x%08x) ReJIT %s (hr=0x%08x)",
                     s.type, s.method, methods[i], SUCCEEDED(hr) ? "ok" : "FAILED", hr);
            }
        }
        import->CloseEnum(en);
    }
    if (attached_) info_->ApplyMetaData(moduleId);  // commit DefineUserString
    if (emit) emit->Release();
    import->Release();
}

void KementProfiler::InstrumentLoadedModules() {
    ICorProfilerModuleEnum* en = nullptr;
    if (FAILED(info_->EnumModules(&en)) || !en) return;
    ModuleID ids[64];
    ULONG got = 0;
    while (SUCCEEDED(en->Next(64, ids, &got)) && got > 0) {
        for (ULONG i = 0; i < got; ++i) {
            if (tripwireEnabled_) InstrumentSinks(ids[i]);
            if (hooksEnabled_) InstrumentHooks(ids[i]);
        }
        if (got < 64) break;
    }
    en->Release();
}

// ---------------- Generic hook engine ----------------
namespace {
std::wstring WidenAscii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::wstring TrimW(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r')) s.pop_back();
    return s;
}
}

void KementProfiler::LoadHooks(const std::wstring& path) {
    std::ifstream f(path.c_str());
    if (!f) { KLOG("Hooks: cannot open %ls", path.c_str()); return; }
    std::string raw;
    while (std::getline(f, raw)) {
        std::wstring w = TrimW(WidenAscii(raw));
        if (w.empty() || w[0] == L'#') continue;
        std::vector<std::wstring> p;
        size_t pos = 0;
        for (;;) {
            size_t bar = w.find(L'|', pos);
            if (bar == std::wstring::npos) { p.push_back(TrimW(w.substr(pos))); break; }
            p.push_back(TrimW(w.substr(pos, bar - pos)));
            pos = bar + 1;
        }
        if (p.size() < 2) continue;
        size_t dot = p[0].find_last_of(L'.');
        if (dot == std::wstring::npos) continue;
        HookDef h;
        h.type = p[0].substr(0, dot);
        h.method = p[0].substr(dot + 1);
        const std::wstring& a = p[1];
        if (a == L"returnbool")      { h.action = HookAction::ReturnConst; h.ival = (p.size() > 2 && p[2] == L"1") ? 1 : 0; }
        else if (a == L"returnint")  { h.action = HookAction::ReturnConst; h.ival = p.size() > 2 ? _wtoi(p[2].c_str()) : 0; }
        else if (a == L"returnnull") { h.action = HookAction::ReturnNull; }
        else if (a == L"throw")      { h.action = HookAction::Throw; h.sval = p.size() > 2 ? p[2] : L"blocked"; }
        else if (a == L"trace")      { h.action = HookAction::Trace; }
        else if (a == L"tracethis")  { h.action = HookAction::TraceThis; h.getter = p.size() > 2 ? p[2] : L""; }
        else if (a == L"tracearg")   { h.action = HookAction::TraceArg; h.argIndex = p.size() > 2 ? _wtoi(p[2].c_str()) : 0; h.argType = p.size() > 3 ? p[3] : L""; h.getter = p.size() > 4 ? p[4] : L""; }
        else if (a == L"canary")     { h.action = HookAction::Canary; }
        else continue;
        hooks_.push_back(h);
    }
    hooksEnabled_ = !hooks_.empty();
    KLOG("Hooks: loaded %zu hook(s) from %ls", hooks_.size(), path.c_str());
}

bool KementProfiler::EnsureProbeRefs(ModuleID moduleId, ProbeRefs* out) {
    {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        auto it = probeRefsByModule_.find(moduleId);
        if (it != probeRefsByModule_.end()) { *out = it->second; return it->second.trace2 != mdTokenNil; }
    }
    IMetaDataEmit2* emit = nullptr;
    IMetaDataAssemblyEmit* asmEmit = nullptr;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit2, reinterpret_cast<IUnknown**>(&emit))) || !emit)
        return false;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataAssemblyEmit, reinterpret_cast<IUnknown**>(&asmEmit))) || !asmEmit) {
        emit->Release(); return false;
    }
    ProbeRefs r{ mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil };
    ASSEMBLYMETADATA amd = {0}; amd.usMajorVersion = 1;
    mdAssemblyRef asmRef = mdTokenNil;
    mdTypeRef typeRef = mdTokenNil;
    if (SUCCEEDED(asmEmit->DefineAssemblyRef(nullptr, 0, L"Kement.Probes", &amd, nullptr, 0, 0, &asmRef)) &&
        SUCCEEDED(emit->DefineTypeRefByName(asmRef, L"Kement.Probes.Sink", &typeRef))) {
        const BYTE sHit[]    = { 0x00, 0x02, ELEMENT_TYPE_VOID, ELEMENT_TYPE_I8, ELEMENT_TYPE_STRING };
        const BYTE sTrace2[] = { 0x00, 0x02, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING, ELEMENT_TYPE_OBJECT };
        const BYTE sTrace1[] = { 0x00, 0x01, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING };
        const BYTE sFail[]   = { 0x00, 0x01, ELEMENT_TYPE_VOID, ELEMENT_TYPE_STRING };
        emit->DefineMemberRef(typeRef, L"Hit",    sHit,    sizeof(sHit),    &r.hit);
        emit->DefineMemberRef(typeRef, L"Trace",  sTrace2, sizeof(sTrace2), &r.trace2);
        emit->DefineMemberRef(typeRef, L"Trace",  sTrace1, sizeof(sTrace1), &r.trace1);
        emit->DefineMemberRef(typeRef, L"Canary", sHit,    sizeof(sHit),    &r.canary);
        emit->DefineMemberRef(typeRef, L"Fail",   sFail,   sizeof(sFail),   &r.fail);
    }
    if (attached_) info_->ApplyMetaData(moduleId);
    emit->Release(); asmEmit->Release();

    std::lock_guard<std::mutex> lock(sinkMutex_);
    probeRefsByModule_[moduleId] = r;
    *out = r;
    return r.trace2 != mdTokenNil;
}

void KementProfiler::InstrumentHooks(ModuleID moduleId) {
    IMetaDataImport* import = nullptr;
    if (FAILED(info_->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, reinterpret_cast<IUnknown**>(&import))) || !import)
        return;
    IMetaDataEmit2* emit = nullptr;
    info_->GetModuleMetaData(moduleId, ofRead | ofWrite, IID_IMetaDataEmit2, reinterpret_cast<IUnknown**>(&emit));

    for (size_t hi = 0; hi < hooks_.size(); ++hi) {
        const HookDef& h = hooks_[hi];
        mdTypeDef td = mdTypeDefNil;
        if (FAILED(import->FindTypeDefByName(h.type.c_str(), mdTokenNil, &td))) continue;

        HCORENUM en = nullptr;
        mdMethodDef methods[64];
        ULONG got = 0;
        while (SUCCEEDED(import->EnumMethodsWithName(&en, td, h.method.c_str(), methods, 64, &got)) && got > 0) {
            for (ULONG i = 0; i < got; ++i) {
                mdMethodDef m = methods[i];
                HookSite site{ hi, mdTokenNil, mdTokenNil, mdTokenNil };

                // Label + message user strings (probe-based actions).
                if (emit && h.action != HookAction::ReturnConst && h.action != HookAction::ReturnNull) {
                    std::wstring label = h.type + L"." + h.method;
                    emit->DefineUserString(label.c_str(), static_cast<ULONG>(label.size()), &site.labelUs);
                    if (h.action == HookAction::Throw)
                        emit->DefineUserString(h.sval.c_str(), static_cast<ULONG>(h.sval.size()), &site.msgUs);
                    ProbeRefs pr{};
                    EnsureProbeRefs(moduleId, &pr);
                }
                // Resolve a property getter for trace-prop actions.
                if (h.action == HookAction::TraceThis && !h.getter.empty()) {
                    std::wstring g = L"get_" + h.getter;
                    import->FindMethod(td, g.c_str(), nullptr, 0, &site.getterToken);
                } else if (h.action == HookAction::TraceArg && !h.getter.empty()) {
                    mdTypeDef argTd = mdTypeDefNil;
                    if (SUCCEEDED(import->FindTypeDefByName(h.argType.c_str(), mdTokenNil, &argTd))) {
                        std::wstring g = L"get_" + h.getter;
                        import->FindMethod(argTd, g.c_str(), nullptr, 0, &site.getterToken);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(sinkMutex_);
                    hookMethods_[{moduleId, m}] = site;
                }
                ModuleID mods[1] = { moduleId };
                mdMethodDef toks[1] = { m };
                HRESULT hr = attached_ && info10_
                    ? info10_->RequestReJITWithInliners(COR_PRF_REJIT_BLOCK_INLINING, 1, mods, toks)
                    : info_->RequestReJIT(1, mods, toks);
                KLOG("Hook: %ls::%ls (token=0x%08x) ReJIT %s", h.type.c_str(), h.method.c_str(), m, SUCCEEDED(hr) ? "ok" : "FAILED");
            }
        }
        import->CloseEnum(en);
    }
    if (attached_) info_->ApplyMetaData(moduleId);
    if (emit) emit->Release();
    import->Release();
}

void KementProfiler::DumpInMemoryModule(ModuleID moduleId, const std::wstring& name,
                                        const BYTE* base, DWORD flags) {
    bool flat = (flags & COR_PRF_MODULE_FLAT_LAYOUT) != 0;

    std::vector<BYTE> image;
    if (!ReconstructPE(base, flat, image) || image.empty()) {
        KLOG("Unpacker: no valid PE at base for module \"%ls\" (flags=0x%08x)",
             name.empty() ? L"<in-memory>" : name.c_str(), flags);
        return;
    }

    int idx = dumpIndex_++;
    std::wstring leaf = name;
    size_t slash = leaf.find_last_of(L"\\/");
    if (slash != std::wstring::npos) leaf = leaf.substr(slash + 1);
    if (leaf.empty()) leaf = L"inmemory";

    wchar_t path[1200];
    swprintf(path, 1200, L"%ls\\dump_%02d_%ls", dumpDir_.c_str(), idx, leaf.c_str());
    std::wstring wpath(path);
    if (wpath.size() < 4 || _wcsicmp(wpath.c_str() + wpath.size() - 4, L".dll") != 0)
        wpath += L".dll";

    std::ofstream f(wpath.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) {
        KLOG("Unpacker: cannot write %ls", wpath.c_str());
        return;
    }
    f.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    f.close();

    std::string hash;
    Sha256Hex(image.data(), image.size(), hash);
    const char* fidelity = LooksLikeManagedPE(image) ? "managed" : "suspect";

    KLOG("Unpacker: DUMPED stage %d -> %ls (%llu bytes, %s, %s, sha256=%s)",
         idx, wpath.c_str(), static_cast<unsigned long long>(image.size()),
         flat ? "flat" : "mapped", fidelity, hash.c_str());

    std::wstring manifestPath = dumpDir_ + L"\\manifest.txt";
    std::ofstream m(manifestPath.c_str(), std::ios::app);
    if (m) {
        char line[2048];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "stage=%d\tfile=%ls\tbytes=%llu\tsha256=%s\tlayout=%s\tfidelity=%s\tflags=0x%08x\ttrigger=ModuleLoadFinished\tmodule=%ls\n",
                    idx, wpath.c_str(), static_cast<unsigned long long>(image.size()),
                    hash.c_str(), flat ? "flat" : "mapped", fidelity, flags,
                    name.empty() ? L"<in-memory>" : name.c_str());
        m << line;
    }
}

HRESULT KementProfiler::JITCompilationStarted(FunctionID functionId, BOOL) {
    jitCount_++;

    ClassID classId = 0;
    ModuleID moduleId = 0;
    mdToken token = mdTokenNil;
    if (FAILED(info_->GetFunctionInfo(functionId, &classId, &moduleId, &token)))
        return S_OK;

    WCHAR modName[1024] = {0};
    ULONG cch = 0;
    LPCBYTE base = nullptr;
    AssemblyID asmId = 0;
    if (FAILED(info_->GetModuleInfo(moduleId, &base, 1024, &cch, modName, &asmId)))
        return S_OK;

    if (!ContainsNoCase(std::wstring(modName), targetAssembly_))
        return S_OK; // keep the log readable: only name target-assembly methods

    targetJitCount_++;

    IMetaDataImport* import = nullptr;
    if (SUCCEEDED(info_->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport,
                                           reinterpret_cast<IUnknown**>(&import)))
        && import != nullptr) {
        WCHAR methName[512] = {0};
        ULONG methLen = 0;
        mdTypeDef td = mdTypeDefNil;
        DWORD attr = 0; PCCOR_SIGNATURE sig = nullptr; ULONG sigLen = 0;
        ULONG rva = 0; DWORD implFlags = 0;
        if (SUCCEEDED(import->GetMethodProps(token, &td, methName, 512, &methLen,
                                             &attr, &sig, &sigLen, &rva, &implFlags))) {
            WCHAR typeName[512] = {0};
            ULONG typeLen = 0; DWORD typeFlags = 0; mdToken baseType = mdTokenNil;
            import->GetTypeDefProps(td, typeName, 512, &typeLen, &typeFlags, &baseType);
            KLOG("JIT  %ls::%ls", typeName, methName);
        }
        import->Release();
    }
    return S_OK;
}

HRESULT KementProfiler::ReJITCompilationStarted(FunctionID, ReJITID, BOOL) {
    KLOG("ReJITCompilationStarted");
    return S_OK;
}

ULONG KementProfiler::BuildReturnConstBody(BYTE* out, INT32 value) {
    // IL: ldc.i4 <value> ; ret
    BYTE il[8];
    ULONG n = 0;
    il[n++] = 0x20;                                  // ldc.i4 <int32>
    il[n++] = static_cast<BYTE>(value & 0xFF);
    il[n++] = static_cast<BYTE>((value >> 8) & 0xFF);
    il[n++] = static_cast<BYTE>((value >> 16) & 0xFF);
    il[n++] = static_cast<BYTE>((value >> 24) & 0xFF);
    il[n++] = 0x2A;                                  // ret

    // Tiny method header: (codeSize << 2) | CorILMethod_TinyFormat (0x2).
    out[0] = static_cast<BYTE>((n << 2) | 0x2);
    for (ULONG i = 0; i < n; ++i) out[1 + i] = il[i];
    return 1 + n;
}

HRESULT KementProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId,
                                           ICorProfilerFunctionControl* pFunctionControl) {
    // Generic hook engine takes precedence.
    {
        HookSite site{ 0, mdTokenNil, mdTokenNil, mdTokenNil };
        bool isHook = false;
        {
            std::lock_guard<std::mutex> lock(sinkMutex_);
            auto it = hookMethods_.find({moduleId, methodId});
            if (it != hookMethods_.end()) { site = it->second; isHook = true; }
        }
        if (isHook) {
            const HookDef& h = hooks_[site.hookIndex];
            std::vector<BYTE> body;
            bool built = false;
            if (h.action == HookAction::ReturnConst) {
                BYTE buf[16]; ULONG n = BuildReturnConstBody(buf, h.ival); body.assign(buf, buf + n); built = true;
            } else if (h.action == HookAction::ReturnNull) {
                BuildReturnNullBody(body); built = true;
            } else {
                LPCBYTE hdr = nullptr; ULONG sz = 0;
                if (SUCCEEDED(info_->GetILFunctionBody(moduleId, methodId, &hdr, &sz)) && hdr) {
                    ProbeRefs pr{}; EnsureProbeRefs(moduleId, &pr);
                    std::vector<BYTE> prefix;
                    auto b1 = [&](BYTE x){ prefix.push_back(x); };
                    auto tok = [&](mdToken t){ for (int i=0;i<4;++i) prefix.push_back(static_cast<BYTE>((t>>(8*i))&0xFF)); };
                    if (h.action == HookAction::Throw) {
                        b1(0x72); tok(site.msgUs);        // ldstr <msg>
                        b1(0x28); tok(pr.fail);           // call Fail(string)
                    } else if (h.action == HookAction::Trace) {
                        b1(0x72); tok(site.labelUs);      // ldstr <label>
                        b1(0x02);                         // ldarg.0 (object)
                        b1(0x28); tok(pr.trace2);         // call Trace(string,object)
                    } else if (h.action == HookAction::TraceThis) {
                        if (site.getterToken == mdTokenNil) { b1(0x72); tok(site.labelUs); b1(0x28); tok(pr.trace1); }
                        else {
                            b1(0x72); tok(site.labelUs);
                            b1(0x02);                         // ldarg.0 (this)
                            b1(0x6F); tok(site.getterToken);  // callvirt get_<prop>
                            b1(0x28); tok(pr.trace2);
                        }
                    } else if (h.action == HookAction::TraceArg) {
                        if (site.getterToken == mdTokenNil) { b1(0x72); tok(site.labelUs); b1(0x28); tok(pr.trace1); }
                        else {
                            b1(0x72); tok(site.labelUs);
                            b1(0x0E); b1(static_cast<BYTE>(h.argIndex));  // ldarg.s <idx>
                            b1(0x6F); tok(site.getterToken);
                            b1(0x28); tok(pr.trace2);
                        }
                    } else if (h.action == HookAction::Canary) {
                        b1(0x21); for (int i=0;i<8;++i) prefix.push_back(0); prefix[prefix.size()-8] = 1;  // ldc.i8 1
                        b1(0x02);                         // ldarg.0 (path)
                        b1(0x28); tok(pr.canary);         // call Canary(long,string)
                    }
                    built = RebuildWithPrefix(hdr, prefix.data(), prefix.size(), 3, body);
                }
            }
            if (built) {
                HRESULT hr = pFunctionControl->SetILFunctionBody(static_cast<ULONG>(body.size()), body.data());
                KLOG("Hook applied: token=0x%08x action=%d (%zu bytes) %s",
                     methodId, static_cast<int>(h.action), body.size(), SUCCEEDED(hr) ? "ok" : "FAIL");
                return hr;
            }
            return S_OK;
        }
    }

    // Tripwire sink: wrap the original body with a probe call.
    INT64 sinkId = -1;
    mdString usToken = mdTokenNil;
    mdMemberRef hitRef = mdTokenNil;
    ConsoleRefs cr{ mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil, mdTokenNil };
    {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        auto it = sinkMethods_.find({moduleId, methodId});
        if (it != sinkMethods_.end()) {
            sinkId = it->second.id;
            usToken = it->second.usToken;
            auto rit = hitRefByModule_.find(moduleId);
            if (rit != hitRefByModule_.end()) hitRef = rit->second;
            auto cit = consoleRefsByModule_.find(moduleId);
            if (cit != consoleRefsByModule_.end()) cr = cit->second;
        }
    }

    if (sinkId >= 0) {
        LPCBYTE origHeader = nullptr;
        ULONG origSize = 0;
        HRESULT hr = info_->GetILFunctionBody(moduleId, methodId, &origHeader, &origSize);
        if (FAILED(hr) || origHeader == nullptr) {
            KLOG("Tripwire: GetILFunctionBody failed for token=0x%08x (hr=0x%08x)", methodId, hr);
            return hr;
        }
        bool blockOnAttach = attachMode_ == L"block" && cr.denyUs != mdTokenNil &&
                             cr.contains != mdTokenNil && cr.exnCtor != mdTokenNil;
        std::vector<BYTE> body;
        bool built = attached_
            ? BuildSinkAttachIL(origHeader, usToken, cr.concat, cr.writeLine, blockOnAttach,
                                cr.denyUs, cr.contains, cr.exnCtor, cr.msgUs, body)
            : (hitRef != mdTokenNil && BuildSinkInstrumentedIL(origHeader, sinkId, hitRef, body));
        if (!built) {
            KLOG("Tripwire: IL rewrite failed for token=0x%08x", methodId);
            return E_FAIL;
        }
        hr = pFunctionControl->SetILFunctionBody(static_cast<ULONG>(body.size()), body.data());
        if (SUCCEEDED(hr))
            KLOG("Tripwire: instrumented sink token=0x%08x (sinkId=%lld, %s, %zu IL bytes)",
                 methodId, sinkId, attached_ ? "console" : "probe", body.size());
        else
            KLOG("Tripwire: SetILFunctionBody failed for token=0x%08x (hr=0x%08x)", methodId, hr);
        return hr;
    }

    // M0 ReJIT demo: replace the whole body with `return <retval>`.
    if (rejitEnabled_ && methodId == demoToken_) {
        BYTE body[16];
        ULONG len = BuildReturnConstBody(body, rejitReturnValue_);
        HRESULT hr = pFunctionControl->SetILFunctionBody(len, body);
        if (SUCCEEDED(hr))
            KLOG("ReJIT applied to token=0x%08x: new body returns %d (%lu IL bytes)",
                 methodId, rejitReturnValue_, len);
        else
            KLOG("SetILFunctionBody FAILED (hr=0x%08x)", hr);
        return hr;
    }

    return S_OK;  // unknown method: keep the original body
}

HRESULT KementProfiler::ReJITError(ModuleID, mdMethodDef methodId, FunctionID, HRESULT hrStatus) {
    KLOG("ReJITError token=0x%08x hr=0x%08x", methodId, hrStatus);
    return S_OK;
}
