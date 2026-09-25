using System.Diagnostics;

namespace ProcessTarget;

// Exercises a file write (a WriteAllText overload) and a process spawn
// (Process.Start(string)) so the tripwire's overload-precise selection and
// process.start sink can be seen and, in block mode, prevented.
internal static class Program
{
    private static int Main()
    {
        string path = Path.Combine(Path.GetTempPath(), $"kement_proc_{Environment.ProcessId}.txt");
        File.WriteAllText(path, "x");                       // sink: file.write
        Console.WriteLine("[proc-target] wrote file");

        try
        {
            Process p = Process.Start("hostname")!;         // sink: process.start
            p.WaitForExit();
            Console.WriteLine($"[proc-target] spawn succeeded (hostname exit={p.ExitCode})");
        }
        catch (Exception e)
        {
            Console.WriteLine($"[proc-target] spawn blocked: {e.GetType().Name}");
        }

        try { File.Delete(path); } catch { }
        Thread.Sleep(300);
        return 0;
    }
}
