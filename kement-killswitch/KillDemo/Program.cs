using System.Runtime.CompilerServices;

namespace KillDemo;

// A running service with a feature flag baked in as "on". Kement flips the flag
// off on the live process, no config change, no redeploy, a remote kill switch.
public static class Feature
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool IsEnabled() => true;
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[killdemo] pid={Environment.ProcessId}");
        for (int i = 0; i < 20; i++)
        {
            Console.WriteLine($"[killdemo] iter {i,2}: risky-feature = {(Feature.IsEnabled() ? "ON" : "OFF (killed)")}");
            Thread.Sleep(300);
        }
    }
}
