# kement-test-generator (planned, not built)

Record real method calls in a running app (arguments in, value out) and emit them
as unit tests. Turn a production incident into a permanent failing test.

## Status: not built

## Why it's hard

Same prerequisite as kement-time-machine: capturing the **return value** (epilogue
injection) plus serializing arbitrary argument/return objects. Without the return,
you can't assert on output.

## Sketch

1. Reuse the `capture` action (args + return) from the time-machine work.
2. For each captured call, generate a test: arrange the args, call the method,
   assert the recorded return.
3. Emit xUnit/NUnit source referencing the target types.

Depends on return-capture landing first (shared with kement-time-machine).
