# Kement — .NET Live-Instrumentation Platform (PRD)

Sep 23, 2026 · @Tolga Cohce

## Overview

Kement (working name, Turkish for lasso; it hooks a running process) is a .NET
live-instrumentation platform: a small native core that attaches to any running
.NET process, subscribes to the runtime's internal events, and rewrites method
bodies in memory (via the profiling API and ReJIT), with no source, no recompile,
and no restart of the target.

The core is a means, not the product. On top of it we ship two capabilities:

- **v1 Automatic Unpacker / RE assistant.** Watches a packed or obfuscated .NET
  sample decrypt and JIT its real payload into memory, then dumps the freshly
  emitted assemblies to disk the instant they exist. The sample unpacks itself;
  Kement catches the reveal.
- **v2 Runtime Security Tripwire.** Instruments sensitive sinks (file, socket,
  process spawn, deserialization, dynamic assembly loading) in a live process and
  logs, alerts on, or blocks unexpected access, a lightweight behavioral monitor.

Same engine, two targets: v1 points it at "show me the hidden code," v2 points it
at "tell me when something touches what it shouldn't."

## Architecture

Three layers, bottom to top.

1. **The Core (shared engine).** A native profiler DLL implementing the CLR
   profiling interfaces, loaded into the target at startup (env-var attach) or into
   an already-running process (attach API). It owns the low-level contract with the
   runtime: receiving callbacks, requesting ReJIT, rewriting IL, walking stacks.
2. **Capability modules.** Thin policy layers that tell the Core what to watch and
   what to do (unpacker, tripwire).
3. **The Controller (CLI + output).** Picks a target, selects a capability and its
   config, renders results.

The Core never decides policy; the modules never talk to the runtime directly.

## Milestones

- **M0 Profiler skeleton (the gate).** Native Core loads via env-var attach; logs
  every JITted method; injects a probe into one named method via ReJIT. Proves
  attach + events + rewrite.
- **M1 Unpacker MVP.** Dump assemblies loaded from memory on a known toy self-unpacker.
- **M2 Unpacker hardening.** Multi-stage capture, header reconstruction, run manifest,
  first anti-analysis defeats; validate on a real corpus. v1 ships.
- **M3 Live attach.** Attach to an already-running PID.
- **M4 Tripwire MVP.** Instrument the initial sink set in monitor mode with call
  stacks and argument capture.
- **M5 Tripwire alert/block + polish.** v2 ships.

## Technical approach

- Core in C++ (the profiling API is native COM-style). Controller + modules in C#.
- Modern .NET (CoreCLR), .NET 8+ first. Startup attach via
  `CORECLR_ENABLE_PROFILING` / `CORECLR_PROFILER` / `CORECLR_PROFILER_PATH`; ReJIT
  via `RequestReJIT` + `ICorProfilerFunctionControl`.
- Windows first. .NET Framework support is a later decision.

## Risks

- .NET Framework vs modern .NET attach differences (decide before M2).
- Anti-profiler / anti-analysis arms race: scope to common packers, document coverage.
- Dump fidelity: flag partial dumps.
- Legal / safety: runs live malware; operator supplies the isolated VM.
- ReJIT block-mode stability: block off by default.

Full original PRD text is preserved in the project tracker.
