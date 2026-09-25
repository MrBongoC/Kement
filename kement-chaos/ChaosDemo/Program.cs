using System.Runtime.CompilerServices;

namespace ChaosDemo;

// A healthy service. Kement injects failures into FetchData() to test how the
// app copes, without changing a line of its code.
public static class Worker
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static string FetchData() => "ok-data";
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[chaosdemo] pid={Environment.ProcessId}");
        int ok = 0, fail = 0;
        for (int i = 0; i < 10; i++)
        {
            try { string d = Worker.FetchData(); ok++; Console.WriteLine($"[chaosdemo] iter {i}: {d}"); }
            catch (Exception e) { fail++; Console.WriteLine($"[chaosdemo] iter {i}: FAILED ({e.GetType().Name})"); }
            Thread.Sleep(300);
        }
        Console.WriteLine($"[chaosdemo] summary: ok={ok} failed={fail}");
    }
}
