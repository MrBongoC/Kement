# kement-repl (planned, not built)

A live C# REPL attached to a running .NET process. Type expressions, inspect and
change live objects, while the process runs. "SSH into your app's brain."

## Status: not built

This one is a real multi-week task, not a config on the hook engine, so it is
scaffolded honestly rather than faked.

## Why it's hard

The hook engine can inject a *call*, but a REPL needs to *evaluate arbitrary code*
inside the target. That means loading a scripting engine (Roslyn,
`Microsoft.CodeAnalysis.CSharp.Scripting`) into the target process and feeding it
a two-way channel to the outside.

## Sketch of how it would work

1. Bootstrap a managed agent assembly into the target (the hard part on attach;
   at startup a `DOTNET_STARTUP_HOOKS` agent is easy).
2. Agent hosts Roslyn scripting and opens a named pipe / socket.
3. Controller (`kement repl --pid`) sends expressions; agent evaluates them in the
   target's `AssemblyLoadContext` with access to loaded types, returns results.
4. Optional: resolve the target's own types for autocomplete.

Built on the same Core (attach + rewrite). The blocker is agent bootstrap on
attach, the same gap noted for tripwire-on-attach with non-CoreLib probes.
