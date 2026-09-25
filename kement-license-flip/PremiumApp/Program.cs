using System.Runtime.CompilerServices;

namespace PremiumApp;

// A toy "paid" app. It ships as trial (IsPremium returns false). Kement flips
// that method to return true on a running instance, no file patching.
public static class License
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool IsPremium() => false;
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[premiumapp] pid={Environment.ProcessId}");
        for (int i = 0; i < 20; i++)
        {
            bool p = License.IsPremium();
            Console.WriteLine($"[premiumapp] iter {i,2}: tier = {(p ? "PREMIUM" : "trial")}");
            Thread.Sleep(300);
        }
    }
}
