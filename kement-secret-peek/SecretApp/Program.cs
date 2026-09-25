using System.Runtime.CompilerServices;

namespace SecretApp;

// The app computes a secret but only ever prints a masked version. Kement's
// return capture reads what Reveal() actually returns, revealing the value the
// app never shows.
public static class Secret
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static string Reveal()
    {
        string[] parts = { "S3", "cr3", "t-", "2026" };   // "decrypted" at runtime
        return string.Concat(parts);
    }
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[secretapp] pid={Environment.ProcessId}");
        for (int i = 0; i < 8; i++)
        {
            string s = Secret.Reveal();
            Console.WriteLine($"[secretapp] tick {i}: secret = {new string('*', s.Length)} (never printed in clear)");
            Thread.Sleep(400);
        }
    }
}
