using System.Runtime.CompilerServices;

namespace VaultServer;

// A long-running service whose vault is locked. Nothing in its own code ever
// unlocks it. Kement attaches to the running process and flips Vault.Unlock()
// to true, live, no restart. The service starts printing UNLOCKED mid-run.
public static class Vault
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static bool Unlock() => false;
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[vault] pid={Environment.ProcessId} guarding the vault");
        for (int i = 0; ; i++)
        {
            bool open = Vault.Unlock();
            Console.WriteLine($"[vault] tick {i,2}: vault = {(open ? "UNLOCKED  <-- breached" : "LOCKED")}");
            Thread.Sleep(1000);
        }
    }
}
