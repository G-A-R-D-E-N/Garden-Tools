using System;
using System.Numerics;
using Avalonia.Media;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

public partial class SkeletonView
{
    private RootMotion.Motion? _rootMotion;
    private Vector3 _rootPathOffset;

    public void ShowRootMotion(RootMotion.Motion? motion, Vector3 offset)
    {
        _rootMotion = motion;
        _rootPathOffset = offset;
        InvalidateVisual();
    }

    internal void SetTravelCentre(Vector3 centre)
    {
        _centre = centre;
        NotifyCameraChanged();
    }

    private void DrawRootMotion(DrawingContext context)
    {
        if (_rootMotion is not { Previewable: true } motion) return;
        var pen = new Pen(Brushes.Goldenrod, 2);
        Vector3 Ground(Vector3 point) => (point + _rootPathOffset) with { Z = 0.5f };
        for (int i = 1; i < motion.Samples.Count; i++)
            context.DrawLine(pen, Project(Ground(motion.Samples[i - 1].Position)),
                                 Project(Ground(motion.Samples[i].Position)));
        float arm = Math.Max(2, (float)_fit * 0.08f);
        for (int second = 0; second <= Math.Min(motion.Duration, 3600); second++)
        {
            var transform = RootMotion.TransformAt(motion, second);
            var side = Vector3.TransformNormal(Vector3.UnitX, transform) * arm;
            var at = Ground(transform.Translation);
            context.DrawLine(pen, Project(at - side), Project(at + side));
        }
        var end = RootMotion.TransformAt(motion, motion.Duration);
        var facing = motion.Forward.LengthSquared() > 1e-8f ? Vector3.Normalize(motion.Forward) : Vector3.UnitY;
        var forward = Vector3.TransformNormal(facing, end) * arm * 3;
        var right = Vector3.Cross(forward, Vector3.UnitZ) * 0.3f;
        var start = Ground(end.Translation);
        var tip = start + forward;
        context.DrawLine(pen, Project(start), Project(tip));
        context.DrawLine(pen, Project(tip), Project(tip - forward * 0.3f + right));
        context.DrawLine(pen, Project(tip), Project(tip - forward * 0.3f - right));
    }
}
