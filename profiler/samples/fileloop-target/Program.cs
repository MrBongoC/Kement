// A long-running, unmonitored process that touches file sinks on a timer, so a
// tripwire attached mid-flight has activity to catch. It catches exceptions per
// op so block mode (which throws before the real op) is visible without crashing.
// The "[kement...]" lines in its stdout come from the injected instrumentation.
string path = Path.Combine(Path.GetTempPath(), $"kement_fileloop_{Environment.ProcessId}.txt");
Console.WriteLine($"[fileloop] pid={Environment.ProcessId} looping file ops");

for (int i = 0; ; i++)
{
    bool w = Try(() => File.WriteAllText(path, $"iter {i}"));
    bool r = Try(() => _ = File.ReadAllText(path));
    bool d = Try(() => File.Delete(path));
    Console.WriteLine($"[fileloop] iter {i}: write={O(w)} read={O(r)} delete={O(d)}");
    Thread.Sleep(1000);
}

static bool Try(Action op)
{
    try { op(); return true; }
    catch (UnauthorizedAccessException) { return false; }   // blocked by the tripwire
    catch { return true; }                                   // other errors: not a block
}
static string O(bool ok) => ok ? "ok" : "BLOCKED";
