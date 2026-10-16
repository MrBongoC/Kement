# Kement suite

A native CLR profiler engine (`profiler/`) that attaches to any .NET process,
watches it, and rewrites its methods live, plus a set of standalone tools built on
that engine, each in its own folder.

## The engine

`antigrav/profiler/` is the Kement Core: attach (startup or live PID), JIT/module
events, ReJIT with a real IL rewriter, metadata emit, in-memory PE dumping, and a
generic **hook engine** driven by a config file. Its own capabilities (automatic
unpacker, runtime tripwire) and the milestone proofs live in `profiler/README.md`.

Every tool below points the same engine at a different job. Each folder has a
`run.ps1` that builds a demo, runs it under the Core, and verifies the result.

## Tools (built and verified)

| Folder | What it does | How |
|---|---|---|
| `kement-license-flip` | Flip a license/trial check to "premium" on a running app | `returnbool` hook |
| `kement-killswitch` | Turn a feature flag off on a live process, no redeploy | `returnbool` hook |
| `kement-method-tracer` | Log any method + its argument, app needs no logging | `trace` hook + probe |
| `kement-sql-tracer` | Log every SQL statement a running app executes | `tracethis CommandText` hook + probe |
| `kement-llm-spy` | Log every prompt an app sends to an LLM | `trace` hook + probe |
| `kement-chaos` | Inject failures into a healthy method to test resilience | `throw` hook + probe |
| `kement-ransomware-canary` | Alert when destructive file ops spike | `canary` hook + probe |
| `kement-honeypot` | Full attack trail (sensitive ops + calling method) | tripwire monitor + probe |
| `kement-live-crack` | Attach to a running app and flip its lock check live | `returnbool` hook over live attach |
| `kement-secret-peek` | Read what a method returns, even when never printed | `tracereturn` hook (return capture) |

Run any of them:

```powershell
antigrav\kement-<tool>\run.ps1
```

Each prints its own `RESULT: PASS`.

## Tools (planned, scaffolded, not built)

Real ideas that are multi-week sub-projects, not config on the engine. Each folder
has a README with the plan and the blocker; none contain fake code.

| Folder | Blocker |
|---|---|
| `kement-repl` | Roslyn scripting agent injected into the target |
| `kement-http-expose` | HTTP-listener agent injected into the target |
| `kement-time-machine` | return-value capture (epilogue IL) + serialization |
| `kement-test-generator` | same return-capture prerequisite |

Dropped: an AMSI/ETW tamper alarm. That patching happens in native `amsi.dll`
memory, which a managed CLR profiler cannot see. Out of scope by design.

## Safety and ethics

This engine reaches into and rewrites running programs. Use it on your own
software, in an isolated VM for malware, or on targets you are explicitly
authorized to test. The license-flip demo flips a toy app you build here; do not
point it at software you don't own.
