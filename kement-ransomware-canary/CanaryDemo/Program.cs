namespace CanaryDemo;

// Simulates the file-churn signature of ransomware: rapidly writing and deleting
// many files. Kement's canary counts destructive file ops per second and alerts
// when the rate spikes. (Writes to a temp dir only; deletes what it made.)
public static class Program
{
    public static void Main()
    {
        string dir = Path.Combine(Path.GetTempPath(), $"kement_canary_{Environment.ProcessId}");
        Directory.CreateDirectory(dir);
        Console.WriteLine($"[canarydemo] pid={Environment.ProcessId} churning files in {dir}");

        for (int i = 0; i < 60; i++)
        {
            string f = Path.Combine(dir, $"f{i}.tmp");
            File.WriteAllText(f, "encrypted");
            File.Delete(f);
        }
        Console.WriteLine("[canarydemo] done (60 write+delete pairs, fast)");
        Directory.Delete(dir, true);
        Thread.Sleep(300);
    }
}
