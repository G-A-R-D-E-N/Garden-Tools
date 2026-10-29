using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfEditTextStateRecord(ushort CharacterId, SwfRectRecord Bounds, uint? Color, string Text);

public sealed record SwfFrameLabelRow(int LabelIndex, uint Frame, string Name);

public sealed record SwfInstanceTransformEdit(uint InstanceId, SwfTransformRecord Transform);

public static unsafe class SwfVerbs
{
    public static SwfEditTextStateRecord ReadEditText(SwfEditHistory history, int tagIndex)
    {
        using var use = history.UseForVerbs();
        SwfEditTextInfo info;
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfEditTextRead(use.Pointer, (nuint)tagIndex, &info, &text, &size));
        return new SwfEditTextStateRecord(info.CharacterId, ToRecord(info.Bounds),
            info.HasColor != 0 ? info.Color : (uint?)null, Encoding.UTF8.GetString(Call.TakeBytes(text, size)));
    }

    public static SwfPlanResultRecord ApplyEditText(SwfEditHistory history, int tagIndex, SwfEditTextEdit edit)
    {
        var text = edit.Text is null ? null : Encoding.UTF8.GetBytes(edit.Text);
        using var use = history.UseForVerbs();
        fixed (byte* textBytes = text)
        {
            var fields = new SwfEditTextFields
            {
                HasText = (byte)(text is null ? 0 : 1),
                Text = (IntPtr)textBytes,
                TextSize = (nuint)(text?.Length ?? 0),
                HasColor = (byte)(edit.Color is null ? 0 : 1),
                Color = edit.Color ?? 0,
                HasBounds = (byte)(edit.Bounds is null ? 0 : 1),
                Bounds = ToNative(edit.Bounds),
            };
            SwfPlanResult result;
            Call.Check(RadicleNative.SwfEditTextApply(use.Pointer, (nuint)tagIndex, &fields, &result));
            return ToRecord(result);
        }
    }

    public static IReadOnlyList<SwfFrameLabelRow> FrameLabelRows(SwfEditHistory history, ushort timelineId)
    {
        using var use = history.UseForVerbs();
        IntPtr* rows = null;
        nuint count = 0;
        Call.Check(RadicleNative.SwfFrameLabelRows(use.Pointer, timelineId, &rows, &count));
        return Call.TakeStrings(rows, count).Select(ParseRow).ToArray();
    }

    public static SwfPlanResultRecord AddFrameLabel(SwfEditHistory history, ushort timelineId, uint frame, string name)
    {
        using var use = history.UseForVerbs();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfFrameLabelAdd(use.Pointer, timelineId, frame, name, &result));
        return ToRecord(result);
    }

    public static SwfPlanResultRecord RenameFrameLabel(SwfEditHistory history, ushort timelineId, int labelIndex, string name)
    {
        using var use = history.UseForVerbs();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfFrameLabelRename(use.Pointer, timelineId, (nuint)labelIndex, name, &result));
        return ToRecord(result);
    }

    public static SwfPlanResultRecord RemoveFrameLabel(SwfEditHistory history, ushort timelineId, int labelIndex)
    {
        using var use = history.UseForVerbs();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfFrameLabelRemove(use.Pointer, timelineId, (nuint)labelIndex, &result));
        return ToRecord(result);
    }

    public static string PlacementListJson(SwfEditHistory history, ushort timelineId)
    {
        using var use = history.UseForVerbs();
        byte* text = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfPlacementListJson(use.Pointer, timelineId, &text, &size));
        return Call.TakeText(text, size);
    }

    public static SwfPlanResultRecord ApplyInstanceTransforms(SwfEditHistory history, uint rootFrame,
        IReadOnlyList<SwfInstanceTransformEdit> edits)
    {
        var native = edits.Select(edit => new SwfInstanceTransform
        {
            InstanceId = edit.InstanceId,
            Transform = ToNative(edit.Transform),
        }).ToArray();
        using var use = history.UseForVerbs();
        SwfPlanResult result;
        fixed (SwfInstanceTransform* items = native)
        {
            Call.Check(RadicleNative.SwfApplyInstanceTransforms(use.Pointer, rootFrame, items, (nuint)native.Length, &result));
        }
        return ToRecord(result);
    }

    public static SwfPlanResultRecord ReassignInstanceDepth(SwfEditHistory history, uint rootFrame, uint instanceId,
        ushort destinationDepth, out uint newInstanceId)
    {
        using var use = history.UseForVerbs();
        uint moved = 0;
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfReassignInstanceDepth(use.Pointer, rootFrame, instanceId, destinationDepth, &moved, &result));
        newInstanceId = moved;
        return ToRecord(result);
    }

    public static SwfPlanResultRecord InsertInstanceTransformKeyframe(SwfEditHistory history, uint rootFrame,
        uint instanceId, SwfTransformRecord? transform = null)
    {
        var native = transform is { } value ? ToNative(value) : default;
        using var use = history.UseForVerbs();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfInsertInstanceTransformKeyframe(use.Pointer, rootFrame, instanceId,
            transform is null ? null : &native, &result));
        return ToRecord(result);
    }

    private static SwfFrameLabelRow ParseRow(string row)
    {
        var parts = row.Split('\t', 3);
        return new SwfFrameLabelRow(int.Parse(parts[0]), uint.Parse(parts[1]), parts[2]);
    }

    private static SwfPlanResultRecord ToRecord(SwfPlanResult result) => new(result.Changed != 0, result.Revision);

    private static SwfRectRecord ToRecord(SwfRect rect) => new(rect.XMin, rect.XMax, rect.YMin, rect.YMax);

    private static SwfRect ToNative(SwfRectRecord? rect) => rect is { } value
        ? new SwfRect { XMin = value.XMin, XMax = value.XMax, YMin = value.YMin, YMax = value.YMax }
        : default;

    private static SwfTransform ToNative(SwfTransformRecord transform) => new()
    {
        ScaleX = transform.ScaleX,
        RotateSkew0 = transform.RotateSkew0,
        RotateSkew1 = transform.RotateSkew1,
        ScaleY = transform.ScaleY,
        TranslateX = transform.TranslateX,
        TranslateY = transform.TranslateY,
    };
}
