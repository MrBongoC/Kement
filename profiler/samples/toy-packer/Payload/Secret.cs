namespace Payload;

// The "hidden" code. In a real sample this is the malware that only exists
// in memory after the stub decrypts it. Kement's job is to dump this assembly
// to disk the moment the runtime materializes it.
public static class Secret
{
    public const string Marker = "kement-unpacked-payload-v1";

    public static string Reveal() => Marker;
}
