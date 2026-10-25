using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using Radicle.Interop;

namespace FO4RecordEditor.Services;

// Constraint: the panel talks to one open document at a time; every method returns JSON with an "error" field on refusal.
[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
[RpcExpose]
public class SwfInterop
{
    private static SwfDocument? _document;
    private static readonly object Gate = new();

    public Task<string> Open(string path) => Run(() =>
    {
        lock (Gate)
        {
            _document?.Dispose();
            _document = SwfDocument.OpenFile(path);
            return Snapshot();
        }
    });

    public Task<string> Characters() => Run(() => Json(new
    {
        characters = Document().Characters().Select(c => new
        {
            id = c.CharacterId, kind = c.Kind.ToString(), tagIndex = c.TagIndex, name = c.Name ?? "",
            imported = c.ImportedName ?? "",
        }).ToArray(),
    }));

    public Task<string> FrameLabels() => Run(() => Json(new
    {
        labels = Document().FrameLabels().Select(l => new
        {
            name = l.Name, tagIndex = l.TagIndex, spriteId = l.SpriteId, insideSprite = l.InsideSprite,
        }).ToArray(),
    }));

    public Task<string> Frame(int timeline, int frame) => Run(() => Json(new
    {
        rows = Document().Frame(timeline, frame).Select(i => new
        {
            depth = i.Depth, character = i.CharacterId, name = i.Name ?? "",
            x = i.Transform.TranslateX, y = i.Transform.TranslateY,
            scaleX = i.Transform.ScaleX, scaleY = i.Transform.ScaleY,
            rotation = i.Transform.RotateSkew0, instanceId = i.InstanceId,
        }).ToArray(),
    }));

    public Task<string> RenameLabel(int tagIndex, string name) => Run(() =>
    {
        var document = Document();
        var payload = System.Text.Encoding.ASCII.GetBytes(name + "\0");
        var result = document.Apply(new SwfPlanSpec().Replace(tagIndex, payload));
        return Snapshot(result.Changed);
    });

    public Task<string> ReadEditText(int tagIndex) => Run(() =>
    {
        var text = Document().EditText(tagIndex);
        return Json(new
        {
            id = text.CharacterId, text = text.Text, variable = text.Variable,
            color = text.Color is null ? "" : text.Color.Value.ToString("X8"),
            x0 = text.Bounds.XMin, x1 = text.Bounds.XMax, y0 = text.Bounds.YMin, y1 = text.Bounds.YMax,
        });
    });

    public Task<string> EditEditText(int tagIndex, string text, string colorHex,
                                     int x0, int x1, int y0, int y1) => Run(() =>
    {
        uint? color = string.IsNullOrWhiteSpace(colorHex)
            ? null
            : Convert.ToUInt32(colorHex.TrimStart('#'), 16);
        var result = Document().EditEditText(tagIndex,
            new SwfEditTextEdit(text, color, new SwfRectRecord(x0, x1, y0, y1)));
        return Snapshot(result.Changed);
    });
    public Task<string> Placements() => Run(() => Json(new
    {
        placements = Document().Placements().Select(item => new
        {
            tagIndex = item.TagIndex, depth = item.Depth, character = item.CharacterId,
            insideSprite = item.InsideSprite,
        }).ToArray(),
    }));

    public Task<string> SetDepth(int tagIndex, int depth) => Run(() =>
        Snapshot(Document().SetDepth(tagIndex, (ushort)depth).Changed));
    public Task<string> PlacementMatrix(int tagIndex) => Run(() =>
    {
        var matrix = Document().PlacementTransform(tagIndex);
        return Json(new
        {
            hasScale = matrix.HasScale, scaleX = matrix.ScaleX, scaleY = matrix.ScaleY,
            hasRotate = matrix.HasRotate, rotate0 = matrix.Rotate0, rotate1 = matrix.Rotate1,
            x = matrix.TranslateX, y = matrix.TranslateY,
        });
    });

    public Task<string> SetTransform(int tagIndex, int x, int y, int scaleX, int scaleY, int rotate0, int rotate1) =>
        Run(() =>
        {
            var result = Document().SetPlacementTransform(tagIndex,
                new SwfPlacementTransformEdit(x, y, scaleX, scaleY, rotate0, rotate1));
            return Snapshot(result.Changed);
        });
    public Task<string> AddLabel(int frame, string name) => Run(() =>
    {
        var result = Document().AddLabel(frame, name);
        return Snapshot(result.Changed);
    });
    public Task<string> Undo() => Run(() => { Document().Undo(); return Snapshot(false); });

    public Task<string> Redo() => Run(() => { Document().Redo(); return Snapshot(false); });

    public Task<string> State() => Run(() => Snapshot(false));

    public Task<string> Save() => Run(() => Json(new { saved = Document().Save() }));

    private static SwfDocument Document() =>
        _document ?? throw new InvalidOperationException("no SWF document is open");

    private static string Snapshot(bool changed = false)
    {
        var document = _document ?? throw new InvalidOperationException("no SWF document is open");
        return Json(new
        {
            changed,
            revision = document.Revision,
            undo = document.UndoCount,
            redo = document.RedoCount,
            dirty = document.Dirty,
        });
    }

    private static string Json(object value) => JsonSerializer.Serialize(value);

    private static Task<string> Run(Func<string> action) => Task.Run(() =>
    {
        try { return action(); }
        catch (RadicleException ex) { return Json(new { error = ex.Message, status = ex.Status.ToString() }); }
        catch (Exception ex)
        {
            DebugLog.Exception("Swf", ex);
            return Json(new { error = ex.Message });
        }
    });
}