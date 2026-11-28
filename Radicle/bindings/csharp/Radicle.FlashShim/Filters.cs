namespace Flash.Filters;

public abstract class BitmapFilter
{
    public abstract BitmapFilter Clone();
}

public class BlurFilter : BitmapFilter
{
    public BlurFilter(double blurX = 4, double blurY = 4, int quality = 1)
    {
        BlurX = blurX;
        BlurY = blurY;
        Quality = quality;
    }

    public double BlurX { get; set; }

    public double BlurY { get; set; }

    public int Quality { get; set; }

    public override BitmapFilter Clone() => new BlurFilter(BlurX, BlurY, Quality);
}

public class DropShadowFilter : BitmapFilter
{
    public DropShadowFilter(double distance = 4, double angle = 45, uint color = 0, double alpha = 1, double blurX = 4,
                            double blurY = 4, double strength = 1, int quality = 1, bool inner = false,
                            bool knockout = false, bool hideObject = false)
    {
        Distance = distance;
        Angle = angle;
        Color = color;
        Alpha = alpha;
        BlurX = blurX;
        BlurY = blurY;
        Strength = strength;
        Quality = quality;
        Inner = inner;
        Knockout = knockout;
        HideObject = hideObject;
    }

    public double Distance { get; set; }

    public double Angle { get; set; }

    public uint Color { get; set; }

    public double Alpha { get; set; }

    public double BlurX { get; set; }

    public double BlurY { get; set; }

    public double Strength { get; set; }

    public int Quality { get; set; }

    public bool Inner { get; set; }

    public bool Knockout { get; set; }

    public bool HideObject { get; set; }

    public override BitmapFilter Clone() =>
        new DropShadowFilter(Distance, Angle, Color, Alpha, BlurX, BlurY, Strength, Quality, Inner, Knockout,
                             HideObject);
}

public class GlowFilter : BitmapFilter
{
    public GlowFilter(uint color = 0xFF0000, double alpha = 1, double blurX = 6, double blurY = 6, double strength = 2,
                      int quality = 1, bool inner = false, bool knockout = false)
    {
        Color = color;
        Alpha = alpha;
        BlurX = blurX;
        BlurY = blurY;
        Strength = strength;
        Quality = quality;
        Inner = inner;
        Knockout = knockout;
    }

    public uint Color { get; set; }

    public double Alpha { get; set; }

    public double BlurX { get; set; }

    public double BlurY { get; set; }

    public double Strength { get; set; }

    public int Quality { get; set; }

    public bool Inner { get; set; }

    public bool Knockout { get; set; }

    public override BitmapFilter Clone() => new GlowFilter(Color, Alpha, BlurX, BlurY, Strength, Quality, Inner, Knockout);
}

public class ColorMatrixFilter : BitmapFilter
{
    public ColorMatrixFilter(AsArray? matrix = null)
    {
        Matrix = matrix;
    }

    public AsArray? Matrix { get; set; }

    public override BitmapFilter Clone() => new ColorMatrixFilter(Matrix);
}
