# kement-http-expose (planned, not built)

Expose any internal method of a running app as an HTTP endpoint on the fly. Call a
private function from Postman without the app offering it. Instant admin panel.

## Status: not built

## Why it's hard

Needs a managed agent injected into the target that (a) starts an HTTP listener
and (b) uses reflection to dispatch requests to arbitrary methods, marshalling
JSON to/from their parameters. That is an in-target agent, not a single injected
call, so it shares the REPL's agent-bootstrap requirement.

## Sketch

1. Inject an agent assembly (startup hook is easy; attach needs bootstrap).
2. Agent starts `HttpListener` on a chosen port.
3. Route `/invoke/{Type}/{Method}` to `MethodInfo.Invoke` with JSON args.
4. Auto-generate a tiny index page listing discovered methods as buttons.

Built on the same Core. Blocker: in-target agent bootstrap.
