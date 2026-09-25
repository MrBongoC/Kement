# kement-time-machine (planned, not built)

Record a method's exact inputs (and returns) when it runs, then replay that exact
call offline, as many times as you want, to debug a crash you only saw once.

## Status: not built

## Why it's hard

Needs two things the current hook engine doesn't do yet:
1. Return-value capture. Today we inject a *prefix* (before the body), which sees
   arguments but not the return. Capturing returns needs epilogue injection
   (rewrite every `ret`, or wrap the body in try/finally), which is real IL work.
2. Serialization of arbitrary argument/return objects to a durable form that can
   be rehydrated for replay.

## Sketch

1. Add a `capture` action: log args on entry and the return on exit (epilogue).
2. Serialize args/returns (JSON or binary) to a capture file, keyed by call.
3. A replay harness loads a capture, reconstructs the args, and re-invokes the
   method in isolation.

Return capture is the shared prerequisite with kement-test-generator.
