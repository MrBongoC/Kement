namespace TripwireTarget;

// A benign target that touches file sinks so the Kement tripwire can catch them.
internal static class Program
{
    private static int Main()
    {
        Console.WriteLine($"[tw-target] pid={Environment.ProcessId} touching file sinks");
        string path = Path.Combine(Path.GetTempPath(), $"kement_tw_{Environment.ProcessId}.txt");

        File.WriteAllText(path, "secret-data");     // sink: file.write
        string s = File.ReadAllText(path);          // sink: file.read
        File.AppendAllText(path, "-more");          // sink: file.append
        File.Delete(path);                          // sink: file.delete

        Console.WriteLine($"[tw-target] done ({s.Length} chars read from {path})");
        Thread.Sleep(500);                          // let the probe flush
        return 0;
    }
}
