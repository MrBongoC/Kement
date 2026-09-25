using System.Runtime.CompilerServices;

namespace LlmDemo;

// Stands in for an app that calls an LLM SDK. Complete() is where the prompt goes
// out to the model. Kement traces every prompt the app sends, from outside.
// (Real SDK calls are instance methods; this demo uses a static one so the prompt
// is arg0. For a real SDK you'd hook the send method with `tracearg <index>`.)
public static class LlmClient
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static string Complete(string prompt) => $"(model reply to {prompt.Length} chars)";
}

public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[llmdemo] pid={Environment.ProcessId} talking to 'the model'");
        string[] prompts =
        {
            "Summarize the quarterly report",
            "Translate 'hello' to Turkish",
            "Write a SQL query for top customers",
        };
        foreach (string p in prompts)
        {
            string _ = LlmClient.Complete(p);
            Thread.Sleep(300);
        }
        Console.WriteLine("[llmdemo] done");
        Thread.Sleep(300);
    }
}
