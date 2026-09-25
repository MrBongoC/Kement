using System.Runtime.CompilerServices;

namespace HoneypotDemo;

// Simulates a web handler that got exploited: attacker-controlled input drives it
// to drop a webshell and read a sensitive file. Kement's tripwire (monitor mode)
// records the whole trail, each sink hit tagged with the calling method.
public static class RequestHandler
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Handle(string attackerInput)
    {
        string shell = Path.Combine(Path.GetTempPath(), $"webshell_{Environment.ProcessId}.aspx");
        File.WriteAllText(shell, "<% eval(request) %>");   // drop a webshell
        _ = File.ReadAllText(shell);                        // read it back
        File.Delete(shell);                                 // clean up
    }
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[honeypot-demo] pid={Environment.ProcessId} handling a 'malicious request'");
        RequestHandler.Handle("'; DROP TABLE users; --");
        Console.WriteLine("[honeypot-demo] done");
        Thread.Sleep(400);
    }
}
