using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;

namespace Packer;

// Toy self-unpacker: the on-disk exe is a stub. The real assembly (Payload.dll)
// is AES-encrypted and embedded. At runtime the stub decrypts it in memory and
// hands the bytes to Assembly.Load(byte[]) -- never touching disk. This is the
// reveal Kement is meant to catch.
public static class Program
{
    // Shared with pack.ps1. A toy fixed key/IV; a real sample derives these.
    private static readonly byte[] Key = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    private static readonly byte[] IV  = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODw==");

    public static int Main(string[] args)
    {
        Console.WriteLine($"[packer] pid={Environment.ProcessId} starting");

        byte[] cipher = ReadEmbedded("payload.enc");
        Console.WriteLine($"[packer] embedded ciphertext: {cipher.Length} bytes");

        byte[] plain = Decrypt(cipher);
        using (var sha = SHA256.Create())
            Console.WriteLine($"[packer] decrypted payload: {plain.Length} bytes, sha256={Convert.ToHexString(sha.ComputeHash(plain)).ToLowerInvariant()}");

        // Loop mode: keep unpacking into fresh load contexts on a timer, so a
        // profiler attached mid-flight (M3) has in-memory modules to catch.
        if (args.Contains("loop"))
        {
            Console.WriteLine("[packer] loop mode: unpacking every 2s (Ctrl+C to stop)");
            for (int i = 0; ; i++)
            {
                var alc = new AssemblyLoadContext($"payload-{i}", isCollectible: true);
                Assembly a = alc.LoadFromStream(new MemoryStream(plain));
                object? r = a.GetType("Payload.Secret")?.GetMethod("Reveal")?.Invoke(null, null);
                Console.WriteLine($"[packer] iteration {i}: loaded {a.GetName().Name} in-memory, payload said: {r}");
                Thread.Sleep(2000);
            }
        }

        // Single-shot: the reveal, load the real assembly straight from memory.
        Assembly asm = Assembly.Load(plain);
        Console.WriteLine($"[packer] Assembly.Load(byte[]) -> {asm.FullName}");

        Type? t = asm.GetType("Payload.Secret");
        object? result = t?.GetMethod("Reveal")?.Invoke(null, null);
        Console.WriteLine($"[packer] payload said: {result}");

        // Give the profiler a moment to observe module load / JIT before exit.
        Thread.Sleep(1500);
        return 0;
    }

    private static byte[] ReadEmbedded(string name)
    {
        using Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
            ?? throw new InvalidOperationException($"missing embedded resource {name} (run pack.ps1 first)");
        using var ms = new MemoryStream();
        s.CopyTo(ms);
        return ms.ToArray();
    }

    private static byte[] Decrypt(byte[] cipher)
    {
        using Aes aes = Aes.Create();
        aes.Key = Key;
        aes.IV = IV;
        aes.Mode = CipherMode.CBC;
        aes.Padding = PaddingMode.PKCS7;
        using ICryptoTransform dec = aes.CreateDecryptor();
        return dec.TransformFinalBlock(cipher, 0, cipher.Length);
    }
}
