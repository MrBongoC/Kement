using System.Runtime.CompilerServices;

namespace TraceDemo;

// A plain app with no logging of its own. Kement traces chosen methods and their
// arguments from outside, without the app cooperating.
public static class Service
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Login(string user) { }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Charge(string card) { }
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[tracedemo] pid={Environment.ProcessId} (no logging of its own)");
        string[] users = { "alice", "bob", "carol" };
        for (int i = 0; i < users.Length; i++)
        {
            Service.Login(users[i]);
            Service.Charge($"4111-****-{1000 + i}");
            Thread.Sleep(300);
        }
        Console.WriteLine("[tracedemo] done");
        Thread.Sleep(300);
    }
}
