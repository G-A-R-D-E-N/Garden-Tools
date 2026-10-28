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
    static SwfInterop() => RadicleNative.EnsureLoaded();

    private static SwfDocument? _document;
    private static readonly object Gate = new();

    // Constraint: a loose path opens directly; anything else goes through the existing PET asset resolver.
    public Task<string> Open(string path) => Run(() =>
    {
        _document?.Dispose();
        _document = null;
        _document = File.Exists(path)
            ? SwfDocument.OpenFile(ToolPaths.RequireDataOrProjectPath(path))
            : SwfDocument.OpenResolved(path, ResolveBytes);
        return Snapshot();
    });

    private static byte[]? ResolveBytes(string path)
    {
        var hit = AssetResolver.Resolve(path);
        var file = hit == null ? null : AssetResolver.Materialize(hit);
        return file == null ? null : File.ReadAllBytes(file);
    }

    public Task<string> Characters() => Run(() => Json(new
    {
        characters = Document().Characters().Select(c => new
        {
            id = c.CharacterId, kind = c.Kind.ToString(), tagIndex = c.TagIndex, name = c.Name ?? "",
            imported = c.ImportedName ?? "",
        }).ToArray(),
    }));

    public Task<string> FrameLabels(int timeline) => Run(() => Json(new
    {
        labels = Document().FrameLabelRows(timeline).Select(l => new
        {
            labelIndex = l.LabelIndex, frame = l.Frame, name = l.Name,
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
            boundsX = i.Bounds.X, boundsY = i.Bounds.Y, boundsWidth = i.Bounds.Width,
            boundsHeight = i.Bounds.Height, hasBounds = i.HasBounds, clipDepth = i.ClipDepth,
        }).ToArray(),
    }));

    public Task<string> Timeline(int index) => Run(() =>
    {
        var timeline = Document().Timeline(index);
        return Json(new { characterId = timeline.CharacterId, frameCount = timeline.FrameCount });
    });

    public Task<string> RenameLabel(int timeline, int labelIndex, string name) => Run(() =>
        Snapshot(Document().RenameLabel(timeline, labelIndex, name).Changed));

    public Task<string> ReadEditText(int tagIndex) => Run(() =>
    {
        var text = Document().EditText(tagIndex);
        return Json(new
        {
            id = text.CharacterId, text = text.Text,
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
            hasScale = matrix.HasScale, scaleX = matrix.HasScale ? matrix.ScaleX : 0x10000,
            scaleY = matrix.HasScale ? matrix.ScaleY : 0x10000,
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
    public Task<string> InsertKeyframe(int tagIndex, int frame) => Run(() =>
        Snapshot(Document().InsertKeyframe(tagIndex, frame).Changed));
    public Task<string> RemoveLabel(int timeline, int labelIndex) => Run(() =>
        Snapshot(Document().RemoveLabel(timeline, labelIndex).Changed));
    public Task<string> AddLabel(int frame, string name, int timeline) => Run(() =>
    {
        var result = Document().AddLabel(frame, name, timeline);
        return Snapshot(result.Changed);
    });
    public Task<string> InsertAbcInstruction(int abc, int body, int offset, string text) => Run(() =>
        Snapshot(Document().InsertAbcInstruction(abc, body, offset, text).Changed));
    public Task<string> ReplaceAbcInstruction(int abc, int body, int offset, string text) => Run(() =>
        Snapshot(Document().ReplaceAbcInstruction(abc, body, offset, text).Changed));
    public Task<string> AbcBlocks() => Run(() => Json(new
    {
        blocks = Document().Movie.AbcBlocks.Select((block, abc) => new
        {
            abc, name = block.Name,
            classes = block.Classes.Select(klass => klass.Name).ToArray(),
            scripts = block.Scripts.Select(script => script.Name).ToArray(),
            methods = Document().Movie.Methods(abc).Select((method, index) => new
            {
                index, name = method.Name, returnType = method.ReturnType, hasBody = method.HasBody,
            }).ToArray(),
        }).ToArray(),
    }));
    public Task<string> Disassemble(int abc, int methodIndex) => Run(() =>
        Json(new { text = Document().Movie.Disassemble(abc, methodIndex) }));
    public Task<string> InspectText() => Run(() => Json(new { text = Document().InspectText() }));
    public Task<string> ActionTags() => Run(() => Json(new
    {
        tags = Document().Movie.Tags.Where(tag => tag.Code == 12 || tag.Code == 59).Select(tag => new
        {
            tagIndex = tag.Index, code = tag.Code, insideSprite = tag.InsideSprite,
        }).ToArray(),
    }));
    public Task<string> Avm1Disassemble(int tagIndex) => Run(() =>
        Json(new { text = Document().Avm1Disassemble(tagIndex) }));
    public Task<string> ShapeSvg(int characterId) => Run(() =>
    {
        if (characterId < 0 || characterId > ushort.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(characterId), "character id must be 0..65535");
        return Json(new { svg = Document().Movie.ShapeSvg((ushort)characterId) });
    });
    public Task<string> FrameSvg(int frame) => Run(() =>
    {
        if (frame < 0) throw new ArgumentOutOfRangeException(nameof(frame), "frame must be 0 or more");
        return Json(new { svg = Document().Movie.FrameSvg((uint)frame) });
    });
    public Task<string> BitmapPng(int tagIndex) => Run(() =>
        Json(new { png = Convert.ToBase64String(Document().Movie.BitmapPng(tagIndex)) }));
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
        lock (Gate)
        {
            try { return action(); }
            catch (RadicleException ex) { return Json(new { error = ex.Message, status = ex.Status.ToString() }); }
            catch (Exception ex)
            {
                DebugLog.Exception("Swf", ex);
                return Json(new { error = ex.Message });
            }
        }
    });
}