namespace BlockTarget;

// Demonstrates block mode: it creates a file with an uninstrumented API, then
// tries to read and delete it. With the tripwire in block mode and the path on
// the deny list, those sink calls throw and the real operations never happen.
internal static class Program
{
    private static int Main()
    {
        string path = Path.Combine(Path.GetTempPath(), $"kement_block_{Environment.ProcessId}.txt");

        // File.Create is not an instrumented sink, so the file really is created.
        using (FileStream fs = File.Create(path)) { fs.WriteByte(65); }
        Console.WriteLine($"[block-target] created {path} (exists={File.Exists(path)})");

        Console.WriteLine($"[block-target] read blocked  = {TryOp(() => File.ReadAllText(path))}");
        Console.WriteLine($"[block-target] delete blocked = {TryOp(() => File.Delete(path))}");

        bool survived = File.Exists(path);
        Console.WriteLine($"[block-target] file still exists after delete attempt = {survived}");

        // Clean up via an uninstrumented path so the demo leaves nothing behind.
        try { if (survived) new FileInfo(path).Delete(); } catch { }
        Thread.Sleep(300);
        return 0;
    }

    private static bool TryOp(Action op)
    {
        try { op(); return false; }
        catch (UnauthorizedAccessException) { return true; }
        catch (Exception e) { Console.WriteLine($"[block-target]   (threw {e.GetType().Name})"); return false; }
    }
}
