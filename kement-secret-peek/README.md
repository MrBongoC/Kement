# kement-secret-peek

Read what a method **returns**, even when the app never prints it. The demo app
computes a secret and only shows `***********`; Kement's return capture logs the
real value `S3cr3t-2026` that `Secret.Reveal()` hands back.

Run: `run.ps1`. Hook: `hooks.txt` (`tracereturn`). Needs the probe via
`DOTNET_STARTUP_HOOKS`.

How it works: the Core rewrites the method's final `ret` in place to
`dup; ldstr <label>; call TraceReturn(object,string); ret`. Current scope is
single-exit, no-exception-handler, reference-returning methods (the safe case
that needs no branch/EH fixup). Multi-exit and value-type returns need the full
instruction-level IL rewriter (the next upgrade). This is the primitive the
time-machine and test-generator tools were waiting on.
