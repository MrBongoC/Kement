using System.Text;

namespace Kement.Probes;

// The managed probe the Core injects a call to at the top of each instrumented
// sink. It logs the hit, applies policy, and in block mode throws so the real
// sink body never runs. It must never touch an instrumented sink itself (no
// File.*) or it would re-enter: it logs through a raw FileStream, guards
// reentrancy, and reads policy from env once.
public static class Sink
{
    private static readonly object Gate = new();
    private static StreamWriter? _writer;
    [ThreadStatic] private static bool _inProbe;

    private static readonly Dictionary<long, string> Names = new()
    {
        [1] = "file.read",
        [2] = "file.read",
        [3] = "file.write",
        [4] = "file.delete",
        [5] = "process.start",
        [6] = "file.append",
    };

    private enum Mode { Monitor, Alert, Block }
    private static readonly Mode _mode = ParseMode(Env("KEMENT_MODE"));
    private static readonly string[] _deny  = SplitLower(Env("KEMENT_DENY"));
    private static readonly string[] _allow = SplitLower(Env("KEMENT_ALLOW"));

    // Injected IL calls this: ldc.i8 <sinkId>; ldarg.0 (the path/arg); call Hit.
    public static void Hit(long sinkId, string? arg)
    {
        if (_inProbe) return;                 // never instrument ourselves
        _inProbe = true;
        try
        {
            string name = Names.TryGetValue(sinkId, out string? n) ? n : $"sink#{sinkId}";
            bool matched = Matches(arg);
            string verdict = _mode switch
            {
                Mode.Block => matched ? "BLOCK" : "allow",
                Mode.Alert => matched ? "ALERT" : "hit",
                _          => "hit",
            };
            Write($"{DateTime.UtcNow:HH:mm:ss.fff}  {verdict,-6}  {name,-14}  arg={arg}  <- {FirstManagedCaller()}");

            if (verdict == "BLOCK")
                throw new UnauthorizedAccessException($"Kement tripwire blocked {name}: {arg}");
        }
        finally { _inProbe = false; }
    }

    // Generic hook trace: injected as `ldstr label; ldarg.N [.getter]; call Trace`.
    public static void Trace(string label, object? value)
    {
        if (_inProbe) return;
        _inProbe = true;
        try { Write($"{DateTime.UtcNow:HH:mm:ss.fff}  {label}  {value}"); }
        catch { }
        finally { _inProbe = false; }
    }

    public static void Trace(string label)
    {
        if (_inProbe) return;
        _inProbe = true;
        try { Write($"{DateTime.UtcNow:HH:mm:ss.fff}  {label}  <- {FirstManagedCaller()}"); }
        catch { }
        finally { _inProbe = false; }
    }

    // Fault injection: injected as `ldstr msg; call Fail` -- always throws so the
    // real method body never runs.
    public static void Fail(string msg)
    {
        Write($"{DateTime.UtcNow:HH:mm:ss.fff}  [chaos] injected failure: {msg}");
        throw new InvalidOperationException($"Kement chaos: {msg}");
    }

    // Ransomware canary: count destructive file ops in a rolling 1s window and
    // shout when the rate crosses KEMENT_CANARY_RATE (default 10/s).
    private static int _count;
    private static long _windowTicks;
    private static readonly int _rate = int.TryParse(Env("KEMENT_CANARY_RATE"), out int r) ? r : 10;
    public static void Canary(long sinkId, string? arg)
    {
        if (_inProbe) return;
        _inProbe = true;
        try
        {
            long now = DateTime.UtcNow.Ticks;
            if (now - _windowTicks > TimeSpan.TicksPerSecond) { _windowTicks = now; _count = 0; }
            int n = ++_count;
            if (n == _rate + 1)
                Write($"{DateTime.UtcNow:HH:mm:ss.fff}  *** CANARY *** destructive file ops exceeded {_rate}/s (latest: {arg})");
        }
        catch { }
        finally { _inProbe = false; }
    }

    private static bool Matches(string? arg)
    {
        if (arg is null || _deny.Length == 0) return false;
        string a = arg.ToLowerInvariant();
        if (Array.Exists(_allow, x => a.Contains(x))) return false;
        return Array.Exists(_deny, x => a.Contains(x));
    }

    private static string FirstManagedCaller()
    {
        foreach (string raw in Environment.StackTrace.Split('\n'))
        {
            string l = raw.Trim();
            if (l.Length == 0) continue;
            if (l.Contains("Kement.Probes.")) continue;
            if (l.Contains("System.Environment")) continue;
            if (l.Contains("System.IO.File")) continue;
            if (l.Contains("System.Diagnostics.Process")) continue;
            return l;
        }
        return "<unknown>";
    }

    private static void Write(string line)
    {
        lock (Gate)
        {
            _writer ??= OpenWriter();
            _writer?.WriteLine(line);
            _writer?.Flush();
        }
    }

    private static StreamWriter? OpenWriter()
    {
        string? path = Env("KEMENT_PROBE_LOG");
        if (string.IsNullOrEmpty(path)) return null;
        var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        return new StreamWriter(fs, new UTF8Encoding(false)) { AutoFlush = true };
    }

    private static string? Env(string name) => Environment.GetEnvironmentVariable(name);

    private static Mode ParseMode(string? s) => s?.ToLowerInvariant() switch
    {
        "block" => Mode.Block,
        "alert" => Mode.Alert,
        _        => Mode.Monitor,
    };

    private static string[] SplitLower(string? s) =>
        string.IsNullOrEmpty(s)
            ? Array.Empty<string>()
            : s.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
               .Select(x => x.ToLowerInvariant()).ToArray();
}
