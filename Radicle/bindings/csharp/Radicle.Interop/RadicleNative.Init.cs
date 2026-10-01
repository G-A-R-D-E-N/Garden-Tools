namespace Radicle.Interop.Native;

public static unsafe partial class RadicleNative
{
    static RadicleNative() => RadicleLibrary.Register();
}
