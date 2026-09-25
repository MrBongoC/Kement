# kement-llm-spy

Log every prompt a running app sends to an LLM, from outside. The Core hooks the
client's send method and traces the prompt argument.

Run: `run.ps1` (captures 3 prompts from a stub LLM client). Hooks: `hooks.txt`
(`trace`). Needs the probe via `DOTNET_STARTUP_HOOKS`.

The demo uses a static `Complete(string prompt)` so the prompt is arg0. Real SDK
calls are instance methods with the prompt inside a request object; those use the
`tracearg <index> <ArgType> <property>` action instead.
