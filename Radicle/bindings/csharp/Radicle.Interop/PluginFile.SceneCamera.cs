using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed unsafe partial class PluginFile
{
    public (float Fov, float Rate)? ReadSceneActionCamera(FormKey scene, int action)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        byte hasValue;
        float fov, rate;
        Call.Check(RadicleNative.PluginReadSceneActionCamera(use.Pointer, strings.Key(scene), Index(action),
            &hasValue, &fov, &rate));
        return hasValue != 0 ? (fov, rate) : null;
    }

    public bool SetSceneActionCamera(FormKey scene, int action, (float Fov, float Rate)? value)
    {
        var at = Index(action);
        return Edit(scene, (plugin, strings, key) => RadicleNative.PluginSetSceneActionCamera(plugin, key, at,
            value.HasValue ? (byte)1 : (byte)0, value.GetValueOrDefault().Fov, value.GetValueOrDefault().Rate));
    }

}
