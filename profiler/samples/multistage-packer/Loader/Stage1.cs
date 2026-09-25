using System.Reflection;
using System.Security.Cryptography;

namespace Loader;

// Stage 1: itself loaded from memory by the stub, it in turn decrypts and loads
// stage 2 (the real payload) from memory. Two nested reveals for Kement to catch.
public static class Stage1
{
    private static readonly byte[] Key = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
    private static readonly byte[] IV  = Convert.FromBase64String("AAECAwQFBgcICQoLDA0ODw==");

    public static string Run()
    {
        byte[] cipher = ReadEmbedded("stage2.enc");
        byte[] plain = Decrypt(cipher);
        Assembly stage2 = Assembly.Load(plain);
        object? r = stage2.GetType("Payload.Secret")?.GetMethod("Reveal")?.Invoke(null, null);
        return $"stage1 loaded stage2 ({stage2.GetName().Name}); payload said: {r}";
    }

    private static byte[] ReadEmbedded(string name)
    {
        using Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
            ?? throw new InvalidOperationException($"missing {name}");
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
