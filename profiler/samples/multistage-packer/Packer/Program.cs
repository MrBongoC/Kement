using System.Reflection;
using System.Security.Cryptography;

namespace MultiPacker;

// Stage 0 (on disk): decrypts and loads stage 1 (the loader) from memory, which
// then loads stage 2 (the payload) from memory. Kement should dump both stages.
internal static class Program
{
    private static readonly byte[] Key = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    private static readonly byte[] IV  = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODw==");

    private static int Main()
    {
        Console.WriteLine($"[multipacker] pid={Environment.ProcessId} starting");

        byte[] plain = Decrypt(ReadEmbedded("stage1.enc"));
        Assembly stage1 = Assembly.Load(plain);
        Console.WriteLine($"[multipacker] loaded stage1 ({stage1.GetName().Name}) from memory");

        object? result = stage1.GetType("Loader.Stage1")?.GetMethod("Run")?.Invoke(null, null);
        Console.WriteLine($"[multipacker] {result}");

        Thread.Sleep(1000);
        return 0;
    }

    private static byte[] ReadEmbedded(string name)
    {
        using Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
            ?? throw new InvalidOperationException($"missing {name} (run pack-multi.ps1 first)");
        using var ms = new MemoryStream();
        s.CopyTo(ms);
        return ms.ToArray();
    }

    private static byte[] Decrypt(byte[] cipher)
    {
        using Aes aes = Aes.Create();
        aes.Key = Key; aes.IV = IV;
        aes.Mode = CipherMode.CBC; aes.Padding = PaddingMode.PKCS7;
        using ICryptoTransform dec = aes.CreateDecryptor();
        return dec.TransformFinalBlock(cipher, 0, cipher.Length);
    }
}
