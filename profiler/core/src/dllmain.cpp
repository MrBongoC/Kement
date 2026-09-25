#include <windows.h>
#include <atomic>
#include "kement_clsid.h"
#include "KementProfiler.h"
#include "logger.h"

// Global object/lock counts for DllCanUnloadNow.
static std::atomic<LONG> g_objectCount{0};

// Minimal IClassFactory that hands the CLR a KementProfiler instance.
class KementClassFactory : public IClassFactory {
public:
    KementClassFactory() : refCount_(1) { ++g_objectCount; }
    virtual ~KementClassFactory() { --g_objectCount; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --refCount_;
        if (c == 0) delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;
        KementProfiler* profiler = new (std::nothrow) KementProfiler();
        if (profiler == nullptr) return E_OUTOFMEMORY;
        profiler->AddRef();
        HRESULT hr = profiler->QueryInterface(riid, ppv);
        profiler->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override {
        if (fLock) ++g_objectCount; else --g_objectCount;
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_;
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (rclsid != KementProfilerCLSID) return CLASS_E_CLASSNOTAVAILABLE;

    KementClassFactory* factory = new (std::nothrow) KementClassFactory();
    if (factory == nullptr) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_objectCount.load() == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Nothing to do; the CLR drives us through DllGetClassObject.
    }
    return TRUE;
}
