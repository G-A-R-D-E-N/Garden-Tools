using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record ClimateTiming(byte SunriseBegin, byte SunriseEnd, byte SunsetBegin, byte SunsetEnd,
                                    byte Volatility, byte MoonsPhaseLength)
{
    public float SunriseBeginHours => SunriseBegin * (1f / 6f);
    public float SunriseEndHours => SunriseEnd * (1f / 6f);
    public float SunsetBeginHours => SunsetBegin * (1f / 6f);
    public float SunsetEndHours => SunsetEnd * (1f / 6f);
    public byte DaysPerPhase => (byte)(MoonsPhaseLength & 0x3F);
    public bool UsesSecunda => (MoonsPhaseLength & 0x40) != 0;
    public bool UsesMasser => (MoonsPhaseLength & 0x80) != 0;

    public unsafe byte? MoonPhase(uint daysPassed)
    {
        int hasPhase;
        byte phase;
        Call.Check(RadicleNative.ClimateMoonPhase(MoonsPhaseLength, daysPassed, &hasPhase, &phase));
        return hasPhase != 0 ? phase : null;
    }

    public unsafe float VolatilityHoldHours(float multiplier, float durationMin, float durationMax)
    {
        float hours;
        Call.Check(RadicleNative.ClimateVolatilityHoldHours(Volatility, multiplier, durationMin, durationMax, &hours));
        return hours;
    }
}

public sealed record WeatherParameters(byte WindSpeed, byte TransitionDelta, byte SunGlare, byte SunDamage,
                                        byte PrecipitationBegin, byte PrecipitationEnd, byte LightningBegin,
                                        byte LightningEnd, byte LightningFrequency, byte Flags, ByteColor LightningColor,
                                        byte VisualEffectBegin, byte VisualEffectEnd, byte WindDirection,
                                        byte WindDirectionRange, byte? WindTurbulence)
{
    public unsafe float TransitionHours(float minHours, float maxHours)
    {
        float hours;
        Call.Check(RadicleNative.WeatherTransitionHours(TransitionDelta, minHours, maxHours, &hours));
        return hours;
    }
}

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
