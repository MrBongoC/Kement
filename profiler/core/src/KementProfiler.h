#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include "cor.h"
#include "corprof.h"

// Kement Core (M0): the shared native engine.
// Implements ICorProfilerCallback4 (ReJIT callbacks live on v4).
// M0 responsibilities:
//   * attach into a target via env-var startup attach
//   * log JIT-compilation events (proves runtime events fire)
//   * ReJIT one named method and hand the runtime a new IL body
//     (proves the in-memory code-rewrite primitive that v1/v2 build on)
class KementProfiler : public ICorProfilerCallback4 {
public:
    KementProfiler() : refCount_(0) {}
    virtual ~KementProfiler() { if (info10_) info10_->Release(); if (info_) info_->Release(); }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // ICorProfilerCallback -- methods Kement actually implements for M0
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown* pICorProfilerInfoUnk) override;
    HRESULT STDMETHODCALLTYPE Shutdown() override;

    // ICorProfilerCallback3 -- live attach (M3)
    HRESULT STDMETHODCALLTYPE InitializeForAttach(IUnknown* pCorProfilerInfoUnk,
                                                  void* pvClientData, UINT cbClientData) override;
    HRESULT STDMETHODCALLTYPE ProfilerAttachComplete() override;
    HRESULT STDMETHODCALLTYPE ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) override;
    HRESULT STDMETHODCALLTYPE JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock) override;

    // ICorProfilerCallback4 -- ReJIT
    HRESULT STDMETHODCALLTYPE ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock) override;
    HRESULT STDMETHODCALLTYPE GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl) override;
    HRESULT STDMETHODCALLTYPE ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus) override;

    // ---- everything else: no-op stubs (generated from corprof.h) ----
    HRESULT STDMETHODCALLTYPE AppDomainCreationStarted( AppDomainID appDomainId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AppDomainCreationFinished( AppDomainID appDomainId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AppDomainShutdownStarted( AppDomainID appDomainId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AppDomainShutdownFinished( AppDomainID appDomainId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AssemblyLoadStarted( AssemblyID assemblyId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AssemblyLoadFinished( AssemblyID assemblyId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AssemblyUnloadStarted( AssemblyID assemblyId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AssemblyUnloadFinished( AssemblyID assemblyId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ModuleLoadStarted( ModuleID moduleId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ModuleUnloadStarted( ModuleID moduleId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ModuleUnloadFinished( ModuleID moduleId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ModuleAttachedToAssembly( ModuleID moduleId, AssemblyID AssemblyId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ClassLoadStarted( ClassID classId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ClassLoadFinished( ClassID classId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ClassUnloadStarted( ClassID classId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ClassUnloadFinished( ClassID classId, HRESULT hrStatus) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FunctionUnloadStarted( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE JITCompilationFinished( FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchStarted( FunctionID functionId, BOOL *pbUseCachedFunction) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE JITCachedFunctionSearchFinished( FunctionID functionId, COR_PRF_JIT_CACHE result) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE JITFunctionPitched( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE JITInlining( FunctionID callerId, FunctionID calleeId, BOOL *pfShouldInline) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ThreadCreated( ThreadID threadId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ThreadDestroyed( ThreadID threadId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ThreadAssignedToOSThread( ThreadID managedThreadId, DWORD osThreadId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingClientInvocationStarted( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingClientSendingMessage( GUID *pCookie, BOOL fIsAsync) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingClientReceivingReply( GUID *pCookie, BOOL fIsAsync) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingClientInvocationFinished( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingServerReceivingMessage( GUID *pCookie, BOOL fIsAsync) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingServerInvocationStarted( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingServerInvocationReturned( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RemotingServerSendingReply( GUID *pCookie, BOOL fIsAsync) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnmanagedToManagedTransition( FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ManagedToUnmanagedTransition( FunctionID functionId, COR_PRF_TRANSITION_REASON reason) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeSuspendStarted( COR_PRF_SUSPEND_REASON suspendReason) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeSuspendFinished( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeSuspendAborted( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeResumeStarted( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeResumeFinished( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeThreadSuspended( ThreadID threadId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RuntimeThreadResumed( ThreadID threadId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE MovedReferences( ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[ ], ObjectID newObjectIDRangeStart[ ], ULONG cObjectIDRangeLength[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ObjectAllocated( ObjectID objectId, ClassID classId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ObjectsAllocatedByClass( ULONG cClassCount, ClassID classIds[ ], ULONG cObjects[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ObjectReferences( ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RootReferences( ULONG cRootRefs, ObjectID rootRefIds[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionThrown( ObjectID thrownObjectId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionSearchFunctionEnter( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionSearchFunctionLeave( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionSearchFilterEnter( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionSearchFilterLeave( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionSearchCatcherFound( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionOSHandlerEnter( UINT_PTR __unused) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionOSHandlerLeave( UINT_PTR __unused) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionUnwindFunctionEnter( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionUnwindFunctionLeave( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionUnwindFinallyEnter( FunctionID functionId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionUnwindFinallyLeave( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionCatcherEnter( FunctionID functionId, ObjectID objectId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionCatcherLeave( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE COMClassicVTableCreated( ClassID wrappedClassId, REFGUID implementedIID, void *pVTable, ULONG cSlots) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE COMClassicVTableDestroyed( ClassID wrappedClassId, REFGUID implementedIID, void *pVTable) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherFound( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ExceptionCLRCatcherExecute( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ThreadNameChanged( ThreadID threadId, ULONG cchName, WCHAR name[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GarbageCollectionStarted( int cGenerations, BOOL generationCollected[ ], COR_PRF_GC_REASON reason) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SurvivingReferences( ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[ ], ULONG cObjectIDRangeLength[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GarbageCollectionFinished( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinalizeableObjectQueued( DWORD finalizerFlags, ObjectID objectID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RootReferences2( ULONG cRootRefs, ObjectID rootRefIds[ ], COR_PRF_GC_ROOT_KIND rootKinds[ ], COR_PRF_GC_ROOT_FLAGS rootFlags[ ], UINT_PTR rootIds[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE HandleCreated( GCHandleID handleId, ObjectID initialObjectId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE HandleDestroyed( GCHandleID handleId) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ProfilerDetachSucceeded( void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ReJITCompilationFinished( FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE MovedReferences2( ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[ ], ObjectID newObjectIDRangeStart[ ], SIZE_T cObjectIDRangeLength[ ]) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SurvivingReferences2( ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[ ], SIZE_T cObjectIDRangeLength[ ]) override { return S_OK; }

private:
    // Resolve "Namespace.Type" + "Method" in a module to its methodDef token.
    HRESULT ResolveMethodToken(ModuleID moduleId, mdMethodDef* pToken);
    // Build a tiny IL body: `ldc.i4 <val>; ret`. Returns bytes written.
    static ULONG BuildReturnConstBody(BYTE* out, INT32 value);
    // Unpacker: dump an in-memory (non-disk) module's PE image + manifest entry.
    void DumpInMemoryModule(ModuleID moduleId, const std::wstring& name,
                            const BYTE* base, DWORD flags);
    // Shared setup: QI the info interface, set the event mask, log the banner.
    // onAttach picks the attach-safe mask (ReJIT is immutable, startup-only).
    HRESULT CommonInit(IUnknown* infoUnk, bool onAttach);
    // Populate config from KEY=VALUE lines (used for attach client data).
    void ApplyConfigString(const std::wstring& settings);
    // Tripwire (v2): find configured sinks in a module, wire refs, ReJIT them.
    void InstrumentSinks(ModuleID moduleId);
    // Emit (once per module) the AssemblyRef/TypeRef/MemberRef for the probe.
    mdMemberRef EnsureHitRef(ModuleID moduleId);
    // Attach path: resolve CoreLib tokens for logging (+ blocking) and define the
    // block user strings. Returns false if this module isn't CoreLib.
    struct ConsoleRefs;
    bool EnsureConsoleRefs(ModuleID moduleId, ConsoleRefs* out);
    // On attach, instrument sinks in modules already loaded before we attached.
    void InstrumentLoadedModules();

    // Generic hook engine (drives all the standalone tools via a config file).
    void LoadHooks(const std::wstring& path);
    void InstrumentHooks(ModuleID moduleId);
    struct ProbeRefs;
    bool EnsureProbeRefs(ModuleID moduleId, ProbeRefs* out);

    std::atomic<ULONG> refCount_;
    ICorProfilerInfo8* info_ = nullptr;
    ICorProfilerInfo10* info10_ = nullptr;  // for ReJIT-on-attach (RequestReJITWithInliners)

    // Config, read from env in Initialize.
    std::wstring targetAssembly_;  // KEMENT_TARGET_ASSEMBLY (module name substring)
    std::wstring rejitType_;       // KEMENT_REJIT_TYPE  e.g. "TestTarget.Program"
    std::wstring rejitMethod_;     // KEMENT_REJIT_METHOD e.g. "MagicNumber"
    INT32 rejitReturnValue_ = 0;   // KEMENT_REJIT_RETVAL
    bool rejitEnabled_ = false;

    std::atomic<ULONG> jitCount_{0};       // total methods JITted
    std::atomic<ULONG> targetJitCount_{0}; // methods JITted in the target assembly
    bool rejitRequested_ = false;

    // Unpacker (v1) config + state.
    std::wstring dumpDir_;       // KEMENT_DUMP_DIR; set => unpacker enabled
    bool unpackEnabled_ = false;
    std::atomic<int> dumpIndex_{0};

    bool attached_ = false;      // loaded via InitializeForAttach (M3)
    bool rejitOnAttachOk_ = false;  // could enable ReJIT during attach init
    mdMethodDef demoToken_ = mdTokenNil;  // the M0 ReJIT-demo method token

    // Tripwire (v2) config + state.
    bool tripwireEnabled_ = false;
    std::mutex sinkMutex_;
    std::map<ModuleID, mdMemberRef> hitRefByModule_;
    // Attach-path CoreLib tokens (log + block), resolved once per module.
    struct ConsoleRefs {
        mdToken writeLine; mdToken concat;   // Internal.Console.WriteLine, String.Concat
        mdToken contains; mdToken exnCtor;   // String.Contains, UnauthorizedAccessException..ctor
        mdString denyUs; mdString msgUs;     // block: deny term + message user strings
    };
    std::map<ModuleID, ConsoleRefs> consoleRefsByModule_;
    struct SinkEntry { INT64 id; mdString usToken; };  // usToken used on attach only
    std::map<std::pair<ModuleID, mdMethodDef>, SinkEntry> sinkMethods_;

    // Attach-mode policy (native holds it since there's no managed probe on attach).
    std::wstring attachMode_;   // "monitor" | "block"
    std::wstring attachDeny_;   // first deny substring

    // Generic hook engine (KEMENT_HOOKS config file).
    enum class HookAction { ReturnConst, ReturnNull, Throw, Trace, TraceThis, TraceArg, Canary };
    struct HookDef {
        std::wstring type, method;
        HookAction action;
        int ival = 0;             // ReturnConst value
        std::wstring sval;        // Throw message
        int argIndex = 0;         // TraceArg index
        std::wstring argType;     // TraceArg arg type name
        std::wstring getter;      // TraceThis/TraceArg property name (without get_)
    };
    std::vector<HookDef> hooks_;
    bool hooksEnabled_ = false;
    struct HookSite { size_t hookIndex; mdToken getterToken; mdString labelUs; mdString msgUs; };
    std::map<std::pair<ModuleID, mdMethodDef>, HookSite> hookMethods_;
    struct ProbeRefs { mdToken hit; mdToken trace2; mdToken trace1; mdToken canary; mdToken fail; };
    std::map<ModuleID, ProbeRefs> probeRefsByModule_;
};
