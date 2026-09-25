# Kement

.NET live-instrumentation platform. A native CLR profiler core attaches to a
running .NET process, receives runtime events, and rewrites method bodies in
memory via ReJIT, with no source, no recompile, no restart of the target.

Two capabilities sit on the same core:
- **v1 Unpacker** (planned): dump the real assemblies a packed sample produces at runtime.
- **v2 Tripwire** (planned): instrument sensitive sinks in a live process and log/alert/block.

See `docs/` for the full PRD. This repo currently implements **M0**.

## Status

| Milestone | State |
|-----------|-------|
| M0 profiler skeleton (attach + JIT events + ReJIT rewrite) | **done, verified** |
| M1 unpacker MVP (dump in-memory assemblies) | **done, verified** |
| M2 unpacker hardening (machinery) | **done, verified** (real-corpus validation pending) |
| M3 live attach (attach Core to a running PID) | **done, verified** |
| M4 tripwire MVP (instrument sinks, monitor mode) | **done, verified** |
| M5 tripwire alert/block + allow/deny | **done, verified** |

### What M0 proves

Verified by a real run (`scripts/run-unpacker-m0.ps1`) on .NET 8 (win-x64):

- The native Core loads into a target via env-var **startup attach**
  (`CORECLR_ENABLE_PROFILING` / `CORECLR_PROFILER` / `CORECLR_PROFILER_PATH`).
- It receives runtime events and logs every JIT compilation (`ICorProfilerCallback::JITCompilationStarted`).
- It rewrites a named method via **ReJIT**: `RequestReJIT` +
  `ICorProfilerFunctionControl::SetILFunctionBody`. In the test the source method
  `TestTarget.Program.MagicNumber()` returns `111`; the Core replaces its body at
  runtime so the running process observes `222` instead. Exit code `0`, `RESULT: PASS`.

### What M1 proves

Verified by a real run (`scripts/run-unpack.ps1`):

- The toy self-unpacker (`samples/toy-packer`) AES-decrypts an embedded assembly
  in memory and calls `Assembly.Load(byte[])`, never writing the payload to disk.
- The Core sees the resulting in-memory module in `ModuleLoadFinished`
  (flags: not `COR_PRF_MODULE_DISK`, flat layout), reconstructs its PE from the
  base load address, and writes it to the dump dir with a manifest (size, sha256,
  layout, trigger).
- The recovered file is **byte-for-byte identical** to the original `Payload.dll`
  (sha256 matches), so it opens in a decompiler with no header surgery.

### What M3 proves

Verified by a real run (`scripts/run-attach.ps1`):

- The toy unpacker starts as a normal process with **no profiler env vars**.
- The controller (`kement attach --pid <id>`) uses the diagnostics IPC
  `AttachProfiler` command (`Microsoft.Diagnostics.NETCore.Client`) to load the
  Core into that running process; the CLR calls `InitializeForAttach`.
- Config (log path, dump dir) is passed as attach client data, since the target
  has no `KEMENT_*` env.
- After attach the Core catches in-memory modules loaded *afterwards* and dumps
  them byte-for-byte, and logs their JITs.

### Tripwire on live attach

Verified by a real run (`scripts/run-attach-tripwire.ps1`):

- `kement attach --pid <id> --tripwire` instruments the file sinks of an
  already-running, unmonitored process, no restart, no probe assembly.
- Key finding: `COR_PRF_ENABLE_REJIT` (normally startup-only/immutable) **can**
  be set during `InitializeForAttach`, so ReJIT works on attach via
  `ICorProfilerInfo10::RequestReJITWithInliners` + `COR_PRF_REJIT_BLOCK_INLINING`.
  Already-loaded modules are swept in `ProfilerAttachComplete`.
- The probe-bootstrap problem (our managed probe assembly can't be loaded into an
  arbitrary running process) is sidestepped: on attach the injected IL calls
  CoreLib's own `Internal.Console.WriteLine` (a same-assembly call), so file-sink
  hits are logged to the target's stdout with zero metadata emit. In one run,
  30 live hits (`WriteAllText`/`ReadAllText`/`Delete`) with real paths.
- Block on attach too (`scripts/run-attach-block.ps1`): `kement attach --pid <id>
  --tripwire --mode block --deny <substring>` injects, after the log, a
  `String.Contains` check that throws `UnauthorizedAccessException` on a match
  (all CoreLib calls, one short branch). Verified before/after on one live process:
  iters ran `write=ok read=ok delete=ok` before attach, then
  `write=BLOCKED read=BLOCKED delete=BLOCKED` after.
- Scope: file sinks (they live in CoreLib), monitor + block. Alert-on-attach and
  non-CoreLib sinks on attach (e.g. `Process.Start`, a different assembly) still
  use startup attach; they need the cross-assembly probe-ref bootstrap.

### What M4 proves

Verified by a real run (`scripts/run-tripwire.ps1`):

- The Core finds configured sinks in whatever module declares them
  (`System.IO.File.*`), emits an `AssemblyRef`/`TypeRef`/`MemberRef` to the probe
  into that module, and ReJITs each sink.
- In `GetReJITParameters` a real IL rewriter prepends
  `ldc.i8 <sinkId>; ldarg.0; call Kement.Probes.Sink::Hit` to the method,
  rebuilding a fat header and shifting every exception-clause offset.
- Each hit runs the managed probe, which logs the sink, its string argument
  (the path), and the managed caller, e.g.
  `file.read  arg=C:\...\x.txt  <- at TripwireTarget.Program.Main() ...line 12`.
- The probe assembly is made resolvable from framework code via a .NET startup
  hook (`DOTNET_STARTUP_HOOKS`). The target runs to completion (no crash).

Sink selection is signature-precise: every overload that is static with a string
at arg0 is instrumented (via a small COR signature parser), so all the file
overloads fire and non-matching overloads (instance / `ProcessStartInfo`) are
safely skipped. Sinks: `System.IO.File.*` and `System.Diagnostics.Process.Start`.
Network sinks and non-string args come next.

See `scripts/run-process.ps1`: it proves the overload fix (`WriteAllText` now
fires) and the process-spawn sink, monitored, then blocked (`Process.Start` is
short-circuited so `hostname` never runs) while file ops stay allowed.

### What M5 proves

Verified by real runs (`scripts/run-block.ps1`, and alert mode):

- Three modes, set by `KEMENT_MODE` (`monitor` | `alert` | `block`), with path
  `KEMENT_DENY` / `KEMENT_ALLOW` substring lists (allow overrides deny):

  | mode | on a deny match | operation |
  |------|-----------------|-----------|
  | monitor | logs `hit` | runs |
  | alert | logs `ALERT` | runs |
  | block | logs `BLOCK` | **prevented** |

- Block works with no extra IL: the injected `Hit` call throws for a denied op,
  so the real sink body never runs. Proven behaviorally, a denied `File.Delete`
  leaves the file on disk (`still exists = True`), while alert mode with the same
  deny list lets the delete through (`still exists = False`). The target catches
  the block exception and stays up.

### What M2 proves

Verified by real runs:

- **Multi-stage capture** (`scripts/run-unpack-multi.ps1`): a stub loads a loader
  from memory, which loads the payload from memory; the Core dumps both stages in
  load order (`stage=0` loader, `stage=1` payload), each byte-for-byte, each
  flagged `fidelity=managed` in the manifest.
- **Mapped-layout reconstruction** (`core/tests/pe_dump_test.cpp`, built as
  `pe_dump_test.exe`): a synthetic mapped image (sections expanded to their VAs)
  reconstructs byte-for-byte to the original file. Flat pass-through and the
  managed-fidelity check pass too. Run: `pe_dump_test <any managed dll>`.
- **Fidelity flag**: each dump is checked for a CLR (COR20) header and recorded
  as `managed` or `suspect` in the manifest, so partial/native dumps are visible.
- **Anti-analysis env scrub** (`scripts/run-hide.ps1`, opt-in `KEMENT_HIDE=1`):
  after the CLR reads them, the Core clears the `CORECLR_*` vars, so a sample that
  probes the environment to detect a profiler sees nothing. Verified: off → the
  target reads the CLSID; on → it reads `<empty>`.

Still open (the PRD's own question): validation against a **real self-unpacking
corpus**, which needs sourcing and safely handling live samples in an isolated VM.
The dump/reconstruction/fidelity machinery is in place and tested; pointing it at
real malware is an operator step, not something this repo does on its own.

## Layout

```
core/            native C++ profiler DLL (the shared engine)
  inc/           CLR profiling headers vendored from dotnet/runtime release/8.0
  src/           KementProfiler, PE dump (unpacker), IL rewriter (tripwire), exports
controller/      Kement.Cli (C#) -- the `kement` CLI; `attach` drives live attach
probes/          Kement.Probes (C#) -- managed probe the tripwire calls + startup hook
test-target/     C# console app used as the M0 rewrite target
samples/
  toy-packer/       Payload.dll + AES packer stub; M1/M3 dev target (pack.ps1)
  multistage-packer/  stub -> loader -> payload; the M2 multi-stage dev target
  tripwire-target/  benign app that touches file sinks; the M4 dev target
  block-target/     file ops for the M5 block demo
  process-target/   file write + process spawn; overload-precise + process sink
  fileloop-target/  long-running file-ops loop; target for tripwire-on-attach
  hide-check/       reads CORECLR_* to demo the anti-analysis scrub
core/tests/         pe_dump_test: mapped/flat PE reconstruction unit test
scripts/            run harnesses
docs/               PRD
```

## Prerequisites

- Windows x64
- .NET 8 SDK (`dotnet`)
- CMake 3.15+
- MSVC toolset (Visual Studio 2019 Build Tools or newer) + Windows SDK
- A NuGet source for the controller restore (`dotnet nuget add source https://api.nuget.org/v3/index.json -n nuget.org`)

## Build

```powershell
# native Core
cmake -S core -B core/build -G "Visual Studio 16 2019" -A x64
cmake --build core/build --config Release
# -> core/build/Release/Kement.Core.dll

# test target
dotnet build test-target -c Release -o test-target/bin
```

## Run the M0 demo

```powershell
scripts/run-unpacker-m0.ps1
```

The harness sets the profiler env vars, launches the target under the Core, and
prints Kement's log inline. It also writes a log file to `$env:TEMP\kement-m0.log`.

## Run the M1 unpacker demo

```powershell
samples/toy-packer/pack.ps1     # build the toy: Payload.dll + encrypted stub
scripts/run-unpack.ps1          # dump it under the Core and verify the dump
```

`run-unpack.ps1` launches the stub with `KEMENT_DUMP_DIR` set, dumps every
in-memory module it sees, prints the manifest, and checks each dump's sha256
against the original `Payload.dll` (`RESULT: PASS` on a byte-perfect recovery).

## Run the M3 live-attach demo

```powershell
dotnet build controller/Kement.Cli.csproj -c Release -o controller/bin
scripts/run-attach.ps1
```

`run-attach.ps1` starts the toy unpacker looping with no profiler env, attaches
the Core to its PID mid-flight, dumps the in-memory modules it loads after
attach, verifies them against `Payload.dll`, and stops only that PID.

Direct use: `dotnet controller/bin/kement.dll attach --pid <id> --core <Kement.Core.dll> [--dump <dir>] [--tripwire] [--log <file>]`

`scripts/run-attach-tripwire.ps1` proves `--tripwire` on a live process: it starts
an unmonitored file-ops loop, attaches mid-flight, and shows the injected
`[kement-sink] ...` hits in the target's own stdout. `scripts/run-attach-block.ps1`
adds `--mode block --deny <substring>` and shows the ops flip to `BLOCKED` after attach.

## Run the M4 tripwire demo

```powershell
dotnet build probes/Kement.Probes.csproj -c Release -o probes/bin
scripts/run-tripwire.ps1
```

`run-tripwire.ps1` runs a benign target that touches `File.*` under the Core with
`KEMENT_TRIPWIRE=1`, instruments the file sinks, and prints the probe log of
sink hits (each with its path argument and managed caller).

## Run the M5 block-mode demo

```powershell
scripts/run-block.ps1
```

`run-block.ps1` runs the tripwire in `block` mode with a deny list; the target's
denied read and delete are short-circuited (throw before the real op), so the
file survives, proving block prevents the operation, not just logs it.

### Config (env vars read by the Core)

| Var | Meaning |
|-----|---------|
| `CORECLR_ENABLE_PROFILING` | `1` to enable startup attach |
| `CORECLR_PROFILER` | the Core's CLSID `{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}` |
| `CORECLR_PROFILER_PATH` | absolute path to `Kement.Core.dll` |
| `KEMENT_LOG` | log file path (optional; also mirrors to stdout) |
| `KEMENT_DUMP_DIR` | set to arm the unpacker; dumps in-memory modules + manifest here |
| `KEMENT_HIDE` | `1` to scrub `CORECLR_*` env vars after load (anti-analysis) |
| `KEMENT_TRIPWIRE` | `1` to arm the tripwire (instrument sinks) |
| `KEMENT_MODE` | `monitor` (default) / `alert` / `block` |
| `KEMENT_DENY` | `;`-separated path substrings that alert/block match on |
| `KEMENT_ALLOW` | `;`-separated substrings that override deny |
| `KEMENT_PROBE_LOG` | sink-hit log path (read by the managed probe) |
| `DOTNET_STARTUP_HOOKS` | set to `Kement.Probes.dll` so injected sink calls resolve |
| `KEMENT_TARGET_ASSEMBLY` | module-name substring to name in the JIT log (default `TestTarget`) |
| `KEMENT_REJIT_TYPE` | type to ReJIT, e.g. `TestTarget.Program` |
| `KEMENT_REJIT_METHOD` | method to ReJIT, e.g. `MagicNumber` |
| `KEMENT_REJIT_RETVAL` | int the rewritten body returns |

## Notes and known limits (M0)

- **Attach modes.** Startup attach (env vars) and live attach to a running PID
  (`kement attach`) both work. ReJIT currently requires startup attach.
- **Rewrite is a whole-body replace** (`ldc.i4 <val>; ret`, tiny IL header). Real
  IL rewriting of arbitrary methods (prepend/wrap probes, fix exception clauses,
  preserve the original body) needs a proper IL rewriter and comes with M2/M4.
  M0 deliberately keeps the injected body trivial to prove the primitive.
- **Unpacker scope.** Dumps `Assembly.Load(byte[])`-style in-memory modules by
  reconstructing the PE from the base address (flat and mapped layouts), captures
  multi-stage chains in order, and flags dump fidelity. Not yet handled:
  reflection-emit / fully dynamic modules (no contiguous PE), and validation
  against a real malware corpus.
- Headers under `core/inc/` are vendored from dotnet/runtime `release/8.0`.
- CoreCLR-first. .NET Framework attach differs and is a later decision (see PRD Risks).

## Safety

Kement runs live, untrusted code when pointed at malware. It does **not** sandbox
anything; the operator supplies an isolated VM. This is a research / defensive tool.
