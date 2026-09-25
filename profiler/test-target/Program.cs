using System.Runtime.CompilerServices;

namespace TestTarget;

public static class Program
{
    // The method Kement will ReJIT. NoInlining so the JIT keeps it as a real
    // callee whose body ReJIT can replace out from under the running loop.
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static int MagicNumber() => 111;

    public static int Main()
    {
        Console.WriteLine($"[test-target] pid={Environment.ProcessId} starting");
        Console.WriteLine("[test-target] source MagicNumber() returns 111");

        int flipped = -1;
        for (int i = 0; i < 40; i++)
        {
            int v = MagicNumber();
            Console.WriteLine($"[test-target] iteration {i,2}: MagicNumber() = {v}");
            if (v != 111 && flipped < 0)
            {
                flipped = i;
                Console.WriteLine($"[test-target] >>> value changed at iteration {i}: ReJIT took effect");
            }
            Thread.Sleep(200);
        }

        if (flipped >= 0)
        {
            Console.WriteLine($"[test-target] RESULT: PASS (ReJIT observed at iteration {flipped})");
            return 0;
        }
        Console.WriteLine("[test-target] RESULT: no ReJIT observed (value stayed 111)");
        return 2;
    }
}
