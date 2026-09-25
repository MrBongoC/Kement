// Stands in for a sample's anti-analysis check: does the process environment
// reveal a profiler? With KEMENT_HIDE=1 the Core scrubs these after the CLR has
// read them, so a sample probing them sees nothing.
string profiler = Environment.GetEnvironmentVariable("CORECLR_PROFILER") ?? "";
string enable   = Environment.GetEnvironmentVariable("CORECLR_ENABLE_PROFILING") ?? "";
Console.WriteLine($"[hide-check] CORECLR_PROFILER        = {(profiler.Length == 0 ? "<empty>" : profiler)}");
Console.WriteLine($"[hide-check] CORECLR_ENABLE_PROFILING = {(enable.Length == 0 ? "<empty>" : enable)}");
