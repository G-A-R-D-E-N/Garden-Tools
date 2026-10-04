using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record ClimateTiming(byte SunriseBegin, byte SunriseEnd, byte SunsetBegin, byte SunsetEnd,
                                    byte Volatility, byte MoonsPhaseLength);

public sealed record WeatherParameters(byte WindSpeed, byte TransitionDelta, byte SunGlare, byte SunDamage,
                                        byte PrecipitationBegin, byte PrecipitationEnd, byte LightningBegin,
                                        byte LightningEnd, byte LightningFrequency, byte Flags, ByteColor LightningColor,
                                        byte VisualEffectBegin, byte VisualEffectEnd, byte WindDirection,
                                        byte WindDirectionRange, byte? WindTurbulence);

public sealed unsafe partial class RadicleEnvironment
{
    private static ClimateTiming ToTiming(Native.ClimateTiming t) =>
        new(t.SunriseBegin, t.SunriseEnd, t.SunsetBegin, t.SunsetEnd, t.Volatility, t.MoonsPhaseLength);

    private static WeatherParameters ToParameters(Native.WeatherParameters p) =>
        new(p.WindSpeed, p.TransitionDelta, p.SunGlare, p.SunDamage, p.PrecipitationBegin, p.PrecipitationEnd,
            p.LightningBegin, p.LightningEnd, p.LightningFrequency, p.Flags,
            new ByteColor(p.LightningColor.Red, p.LightningColor.Green, p.LightningColor.Blue),
            p.VisualEffectBegin, p.VisualEffectEnd, p.WindDirection, p.WindDirectionRange,
            p.HasWindTurbulence != 0 ? p.WindTurbulence : null);
}
