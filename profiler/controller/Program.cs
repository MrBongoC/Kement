using System.Text;
using Microsoft.Diagnostics.NETCore.Client;

namespace Kement.Cli;

// Kement controller (C# layer). Picks a target and drives the native Core.
// M3: `attach` uses the diagnostics IPC AttachProfiler command to load the
// Core into an already-running .NET process.
internal static class Program
{
    private static readonly Guid ProfilerClsid = new("A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D");

    private static int Main(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
            return Usage();

        return args[0] switch
        {
            "attach" => Attach(args[1..]),
            _ => Usage($"unknown command '{args[0]}'"),
        };
    }

    private static int Attach(string[] args)
    {
        var opt = ParseOptions(args);
        if (!opt.TryGetValue("pid", out string? pidStr) || !int.TryParse(pidStr, out int pid))
            return Usage("attach requires --pid <processId>");

        string core = opt.GetValueOrDefault("core") ?? DefaultCorePath();
        core = Path.GetFullPath(core);
        if (!File.Exists(core)) { Console.Error.WriteLine($"core not found: {core}"); return 1; }

        int timeout = int.TryParse(opt.GetValueOrDefault("timeout"), out int t) ? t : 30;

        // Config for the Core, delivered as client data to InitializeForAttach.
        var sb = new StringBuilder();
        if (opt.TryGetValue("log", out string? log))   sb.Append("LOG=").Append(Path.GetFullPath(log)).Append('\n');
        if (opt.TryGetValue("dump", out string? dump))  sb.Append("DUMP=").Append(Path.GetFullPath(dump)).Append('\n');
        if (opt.TryGetValue("target", out string? tgt)) sb.Append("TARGET=").Append(tgt).Append('\n');
        if (opt.ContainsKey("tripwire"))                sb.Append("TRIPWIRE=1\n");
        if (opt.TryGetValue("mode", out string? mode))  sb.Append("MODE=").Append(mode).Append('\n');
        if (opt.TryGetValue("deny", out string? deny))  sb.Append("DENY=").Append(deny).Append('\n');
        byte[] clientData = Encoding.Unicode.GetBytes(sb.ToString());

        Console.WriteLine($"[kement] attaching Core to pid {pid}");
        Console.WriteLine($"[kement]   core={core}");
        if (log  is not null) Console.WriteLine($"[kement]   log={Path.GetFullPath(log)}");
        if (dump is not null) Console.WriteLine($"[kement]   dump={Path.GetFullPath(dump)}");

        try
        {
            var client = new DiagnosticsClient(pid);
            client.AttachProfiler(TimeSpan.FromSeconds(timeout), ProfilerClsid, core, clientData);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[kement] attach failed: {ex.GetType().Name}: {ex.Message}");
            return 2;
        }

        Console.WriteLine("[kement] attach request accepted by the runtime");
        return 0;
    }

    private static Dictionary<string, string> ParseOptions(string[] args)
    {
        var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--")) continue;
            string key = args[i][2..];
            string val = (i + 1 < args.Length && !args[i + 1].StartsWith("--")) ? args[++i] : "true";
            d[key] = val;
        }
        return d;
    }

    private static string DefaultCorePath()
    {
        string baseDir = AppContext.BaseDirectory;
        return Path.Combine(baseDir, "..", "..", "..", "..", "core", "build", "Release", "Kement.Core.dll");
    }

    private static int Usage(string? err = null)
    {
        if (err is not null) Console.Error.WriteLine($"error: {err}");
        Console.WriteLine("""
            kement - .NET live-instrumentation controller

            usage:
              kement attach --pid <id> [--dump <dir>] [--tripwire] [--log <file>] [--core <dll>] [--timeout <sec>]

            attach loads the Kement Core into an already-running .NET process.
              --dump <dir>   dump in-memory assemblies loaded after attach
              --tripwire     instrument file sinks and log hits to the target's stdout
            """);
        return err is null ? 0 : 64;
    }
}
