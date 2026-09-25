using System.Reflection;
using System.Runtime.Loader;

// .NET startup hook (must be named `StartupHook`, no namespace). The Core's
// injected sink calls reference the Kement.Probes assembly from inside framework
// code; the default load context won't probe the app dir for it, so we make it
// resolvable here. Enabled by setting DOTNET_STARTUP_HOOKS to this dll's path.
internal sealed class StartupHook
{
    public static void Initialize()
    {
        Assembly self = typeof(Kement.Probes.Sink).Assembly;
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
            name.Name == self.GetName().Name ? self : null;
    }
}
