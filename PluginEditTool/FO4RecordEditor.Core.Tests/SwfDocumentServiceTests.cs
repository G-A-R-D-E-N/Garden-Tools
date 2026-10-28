using System;
using System.IO;
using System.Linq;
using FO4RecordEditor.Services;
using Radicle.Interop;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

public class SwfDocumentServiceTests
{
    [NativeFact]
    public void OpensAMovieAndExposesCharactersLabelsAndTheFirstFrame()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        Assert.Equal(2, document.Characters().Count);
        Assert.Equal(new[] { "start", "inner" }, document.FrameLabels().Select(label => label.Name));
        Assert.Equal((ushort)14, document.Timeline(1).CharacterId);
        var frame = document.Frame(0, 0);
        Assert.Single(frame);
        Assert.Equal((ushort)11, frame[0].CharacterId);
        Assert.Equal((ushort)1, frame[0].Depth);
        Assert.False(document.Dirty);
    }

    [NativeFact]
    public void OpensThroughTheHostAssetResolver()
    {
        var requested = "";
        using var document = SwfDocument.OpenResolved("Interface\\Test.swf", path =>
        {
            requested = path;
            return path == "Interface\\Test.swf" ? SwfFixture.Movie() : null;
        });
        Assert.Equal("Interface\\Test.swf", requested);
        Assert.Equal(2, document.Characters().Count);
        Assert.Equal(new[] { "start", "inner" }, document.FrameLabels().Select(label => label.Name));
    }
    [NativeFact]
    public void AnEditIsAHistoryCommandAndUndoRedoFollowIt()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var showFrame = document.Movie.Tags.First(tag => tag.Code == 1 && !tag.InsideSprite);
        var applied = document.Apply(new SwfPlanSpec().Replace(showFrame.Index, new byte[] { 0x00 }));
        Assert.True(applied.Changed);
        Assert.True(document.Dirty);
        Assert.Equal(1, document.UndoCount);

        document.Undo();
        Assert.Equal(0, document.UndoCount);
        Assert.Equal(1, document.RedoCount);
        document.Redo();
        Assert.Equal(1, document.UndoCount);
    }

    [NativeFact]
    public void SavingBacksUpThePreviousBytesBeforeWritingTheEdit()
    {
        var original = SwfFixture.Movie();
        using var directory = new TemporaryDirectory();
        var path = directory.Write("Interface\\Test.swf", original);

        using (var document = SwfDocument.OpenFile(path))
        {
            var label = document.Movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite);
            document.Apply(new SwfPlanSpec().Replace(label.Index, new byte[] { (byte)'m', (byte)'e', (byte)'n', (byte)'u', 0 }));
            var saved = document.Save();
            Assert.Equal(path, saved);
        }

        Assert.True(File.Exists(path + ".bak"));
        Assert.Equal(original, File.ReadAllBytes(path + ".bak"));
        Assert.NotEqual(original, File.ReadAllBytes(path));
    }

    [NativeFact]
    public void EditsEditTextThroughTheSameHistory()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var result = document.EditEditText(0, new SwfEditTextEdit(Text: "hello", Color: 0x80112233u));
        Assert.True(result.Changed);
        var text = document.EditText(0);
        Assert.Equal("hello", text.Text);
        Assert.Equal(0x80112233u, text.Color);
        Assert.Equal("variable", text.Variable);
        document.Undo();
        Assert.Equal(string.Empty, document.EditText(0).Text);
        Assert.Null(document.EditText(0).Color);
    }
    [NativeFact]
    public void AddsAFrameLabelThroughAnInsertionPlan()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var result = document.AddLabel(0, "menu");
        Assert.True(result.Changed);
        Assert.Contains(document.FrameLabels(), label => label.Name == "menu" && !label.InsideSprite);
        document.Undo();
        Assert.DoesNotContain(document.FrameLabels(), label => label.Name == "menu");
    }

    [NativeFact]
    public void RemovesAFrameLabelAndUndoRestoresIt()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var label = document.Movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite);
        var result = document.RemoveLabel(label.Index);
        Assert.True(result.Changed);
        Assert.DoesNotContain(document.FrameLabels(), item => item.Name == "start");
        document.Undo();
        Assert.Contains(document.FrameLabels(), item => item.Name == "start");
    }
    [NativeFact]
    public void AddsALabelOnASpriteTimeline()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var result = document.AddLabel(0, "spriteMenu", 14);
        Assert.True(result.Changed);
        Assert.Contains(document.FrameLabels(), label => label.Name == "spriteMenu" && label.SpriteId == 14);
        document.Undo();
        Assert.DoesNotContain(document.FrameLabels(), label => label.Name == "spriteMenu");
    }
    [NativeFact]
    public void RefusesADuplicateLabelBeforeTouchingTheHistory()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var error = Assert.Throws<InvalidOperationException>(() => document.AddLabel(0, "start"));
        Assert.Contains("already exists", error.Message);
        Assert.Equal(0ul, document.Revision);
    }
    [NativeFact]
    public void InsertsAPlacementKeyframeAndUndoesIt()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var placement = document.Placements().First(item => !item.InsideSprite);
        var before = document.Movie.Tags.Count(tag => tag.Code == 26);
        var result = document.InsertKeyframe(placement.TagIndex, 0);
        Assert.True(result.Changed);
        Assert.Equal(before + 1, document.Movie.Tags.Count(tag => tag.Code == 26));
        var inserted = document.Movie.Tags.Where(tag => tag.Code == 26 && !tag.InsideSprite).Last();
        Assert.Equal(new byte[] { 0x05, 0x01, 0x00, 0x00 }, document.Movie.Payload(inserted.Index));
        var parsed = SwfPlacementCodec.Parse(inserted, document.Movie.Payload(inserted.Index));
        Assert.Equal((ushort)1, parsed.Depth);
        Assert.Null(parsed.CharacterId);
        document.Undo();
        Assert.Equal(before, document.Movie.Tags.Count(tag => tag.Code == 26));
    }

    [NativeFact]
    public void InsertsASpriteKeyframeIntoTheSpritePayload()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var sprite = document.Movie.Tags.First(tag => tag.Code == 39);
        var placement = document.Placements().First(item => item.InsideSprite);
        var before = document.Movie.Tags.Count(tag => tag.Parent == sprite.Index && tag.Code == 26);
        var result = document.InsertKeyframe(placement.TagIndex, 0);
        Assert.True(result.Changed);
        Assert.Equal(before + 1, document.Movie.Tags.Count(tag => tag.Parent == sprite.Index && tag.Code == 26));
        document.Undo();
        Assert.Equal(before, document.Movie.Tags.Count(tag => tag.Parent == sprite.Index && tag.Code == 26));
    }
    [NativeFact]
    public void RefusesAKeyframeOnANonPlacementTag()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var label = document.Movie.Tags.First(tag => tag.Code == 43);
        Assert.Throws<InvalidOperationException>(() => document.InsertKeyframe(label.Index, 0));
        Assert.Equal(0ul, document.Revision);
    }
    [NativeFact]
    public void ARefusalLeavesTheHistoryUntouchedAndCarriesTheMessage()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var error = Assert.Throws<RadicleException>(() =>
            document.Apply(new SwfPlanSpec().Replace(99, new byte[] { 0 })));
        Assert.False(string.IsNullOrWhiteSpace(error.Message));
        Assert.Equal(0ul, document.Revision);
        Assert.Equal(0, document.UndoCount);
        Assert.False(document.Dirty);
    }

    [NativeFact]
    public void AnEditTextEditedTwiceKeepsBothEdits()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        document.EditEditText(0, new SwfEditTextEdit(Text: "hello", Color: 0x80112233u));
        document.EditEditText(0, new SwfEditTextEdit(Text: "hello again"));
        var text = document.EditText(0);
        Assert.Equal("hello again", text.Text);
        Assert.Equal(0x80112233u, text.Color);
        Assert.Equal("variable", text.Variable);
    }

    [NativeFact]
    public void SetPlacementTransformIsUndoable()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var placement = document.Placements().First(item => !item.InsideSprite);
        var result = document.SetPlacementTransform(placement.TagIndex,
            new SwfPlacementTransformEdit(TranslateX: 40, TranslateY: 60));
        Assert.True(result.Changed);
        Assert.Equal(40, document.PlacementTransform(placement.TagIndex).TranslateX);
        Assert.Equal(1, document.UndoCount);
        Assert.True(document.Dirty);
        document.Undo();
        Assert.Equal(0, document.PlacementTransform(placement.TagIndex).TranslateX);
        Assert.False(document.Dirty);
    }

    [NativeFact]
    public void RenameLabelChangesTheNameThroughTheHistory()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var label = document.Movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite);
        var result = document.RenameLabel(label.Index, "begin");
        Assert.True(result.Changed);
        Assert.Contains(document.FrameLabels(), item => item.Name == "begin");
        document.Undo();
        Assert.Contains(document.FrameLabels(), item => item.Name == "start");
    }

    [NativeFact]
    public void RenameLabelRefusesANonLabelTag()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie());
        var placement = document.Placements().First(item => !item.InsideSprite);
        var error = Assert.Throws<InvalidOperationException>(() => document.RenameLabel(placement.TagIndex, "menu"));
        Assert.Contains("not a frame label", error.Message);
        Assert.Equal(0ul, document.Revision);
    }

    [NativeFact]
    public void AnAbcInstructionEditIsAHistoryCommandAndUndoRedoFollowIt()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie(withAbc: true));
        var result = document.InsertAbcInstruction(0, 0, 0, "nop");
        Assert.True(result.Changed);
        Assert.Contains("      0 nop\n", document.Movie.AbcText(0));
        Assert.Equal(1, document.UndoCount);
        document.Undo();
        Assert.DoesNotContain("      0 nop\n", document.Movie.AbcText(0));
        document.Redo();
        Assert.Contains("      0 nop\n", document.Movie.AbcText(0));
    }

    [NativeFact]
    public void ARefusedAbcInstructionLeavesTheHistoryUntouched()
    {
        using var document = SwfDocument.OpenBytes(SwfFixture.Movie(withAbc: true));
        var error = Assert.Throws<RadicleException>(() => document.InsertAbcInstruction(0, 0, 0, "returnvalue"));
        Assert.Contains("verifier refused", error.Message);
        Assert.Equal(0ul, document.Revision);
        Assert.Equal(0, document.UndoCount);
        Assert.False(document.Dirty);
    }

    [NativeFact]
    public void SavingAResolvedAssetWithoutATargetRefuses()
    {
        using var document = SwfDocument.OpenResolved("Interface\\Test.swf", _ => SwfFixture.Movie());
        var error = Assert.Throws<InvalidOperationException>(() => document.Save());
        Assert.Contains("read-only", error.Message);
    }

    [NativeFact]
    public void ASecondSaveKeepsTheFirstBackup()
    {
        var original = SwfFixture.Movie();
        using var directory = new TemporaryDirectory();
        var path = directory.Write("Interface\\Test.swf", original);
        using var document = SwfDocument.OpenFile(path);
        var label = document.Movie.Tags.First(tag => tag.Code == 43 && !tag.InsideSprite).Index;
        document.RenameLabel(label, "menu");
        document.Save();
        document.RenameLabel(label, "other");
        document.Save();
        Assert.Equal(original, File.ReadAllBytes(path + ".bak"));
        Assert.Equal(document.WriteBytes(), File.ReadAllBytes(path));
        Assert.False(document.Dirty);
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "pet-swf-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public string Write(string relative, byte[] contents)
        {
            var file = System.IO.Path.Combine(Path, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(file)!);
            File.WriteAllBytes(file, contents);
            return file;
        }

        public void Dispose()
        {
            try { Directory.Delete(Path, recursive: true); } catch (IOException) { }
        }
    }
}
