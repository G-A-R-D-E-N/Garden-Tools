using System;
using System.Collections.Generic;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfLayoutTests
{
    private static byte[] Matrix(int scale, int x, int y)
    {
        var bits = new SwfBitWriter();
        bits.Push(1, 1);
        bits.Push(19, 5);
        bits.Push((uint)scale, 19);
        bits.Push((uint)scale, 19);
        bits.Push(0, 1);
        bits.Push(16, 5);
        bits.Push((uint)x & 0xFFFFu, 16);
        bits.Push((uint)y & 0xFFFFu, 16);
        return bits.ToArray();
    }

    private static byte[] Place(int depth, int character, byte[] matrix, string? name = null)
    {
        var bytes = new List<byte> { (byte)(0x02 | 0x04 | (name is null ? 0 : 0x20)) };
        bytes.AddRange(SwfFixtures.U16(depth));
        bytes.AddRange(SwfFixtures.U16(character));
        bytes.AddRange(matrix);
        if (name is not null) bytes.AddRange(SwfFixtures.Z(name));
        return bytes.ToArray();
    }

    private static byte[] EditText()
    {
        var bytes = new List<byte>();
        bytes.AddRange(SwfFixtures.U16(11));
        var bits = new SwfBitWriter();
        bits.Rect(0, 400, 0, 200, 12);
        bytes.AddRange(bits.ToArray());
        bytes.Add(0xE5);
        bytes.Add(0x60);
        bytes.AddRange(SwfFixtures.U16(3));
        bytes.AddRange(SwfFixtures.U16(240));
        bytes.AddRange(new byte[] { 0x11, 0x22, 0x33, 0xFF });
        bytes.Add(2);
        bytes.AddRange(new byte[8]);
        bytes.AddRange(SwfFixtures.Z("v"));
        bytes.AddRange(SwfFixtures.Z("Hi"));
        return bytes.ToArray();
    }

    private static byte[] Sprite(int id, int frames, params byte[][] children)
    {
        var bytes = new List<byte>();
        bytes.AddRange(SwfFixtures.U16(id));
        bytes.AddRange(SwfFixtures.U16(frames));
        foreach (var child in children) bytes.AddRange(child);
        bytes.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        return bytes.ToArray();
    }

    private static byte[] Font3(int id, string name)
    {
        var bytes = new List<byte>();
        bytes.AddRange(SwfFixtures.U16(id));
        bytes.Add(0x81);
        bytes.Add(1);
        bytes.Add((byte)name.Length);
        bytes.AddRange(Encoding.ASCII.GetBytes(name));
        bytes.AddRange(SwfFixtures.U16(1));
        bytes.AddRange(SwfFixtures.U16(4));
        bytes.AddRange(SwfFixtures.U16(6));
        bytes.AddRange(new byte[] { 0x10, 0x00 });
        bytes.AddRange(SwfFixtures.U16(0x41));
        bytes.AddRange(SwfFixtures.U16(800));
        bytes.AddRange(SwfFixtures.U16(200));
        bytes.AddRange(SwfFixtures.U16(40));
        bytes.AddRange(SwfFixtures.U16(500));
        var bits = new SwfBitWriter();
        bits.Rect(0, 100, 0, 100, 9);
        bytes.AddRange(bits.ToArray());
        bytes.AddRange(SwfFixtures.U16(0));
        return bytes.ToArray();
    }

    private static byte[] Movie(params byte[][] tags)
    {
        var body = new List<byte>();
        foreach (var tag in tags) body.AddRange(tag);
        var header = new List<byte>();
        header.AddRange(Encoding.ASCII.GetBytes("FWS"));
        header.Add(10);
        header.AddRange(BitConverter.GetBytes((uint)(8 + 9 + 4 + body.Count)));
        var rect = new SwfBitWriter();
        rect.Rect(0, 11000, 0, 8000, 15);
        header.AddRange(rect.ToArray());
        header.AddRange(SwfFixtures.U16(0x0C00));
        header.AddRange(SwfFixtures.U16(1));
        header.AddRange(body);
        return header.ToArray();
    }

    private static byte[] NamedMovie() => Movie(
        SwfFixtures.Tag(37, EditText()),
        SwfFixtures.Tag(43, SwfFixtures.Z("Open")),
        SwfFixtures.Tag(39, Sprite(14, 1,
            SwfFixtures.Tag(26, Place(1, 11, Matrix(0x20000, 200, 100), "Cell")),
            SwfFixtures.Tag(1, Array.Empty<byte>()))),
        SwfFixtures.Tag(26, Place(2, 14, Matrix(0x10000, 0, 0), "Art")),
        SwfFixtures.Tag(1, Array.Empty<byte>()));

    // Regression: the managed record read the native rows by the wrong index or dropped the text field.
    [Fact]
    public void ReadsTheStageStateAndNamedRowsWithComposedRectsAndText()
    {
        using var movie = SwfFile.FromBytes(NamedMovie());
        var layout = movie.Layout();
        Assert.Equal(new SwfBoundsRecord(0, 0, 550, 400), layout.Stage);
        Assert.Equal(12.0, layout.FrameRate);
        Assert.False(layout.Scripted);
        var state = Assert.Single(layout.States);
        Assert.Equal("Open", state.Label);
        Assert.Equal((ushort)0, state.TimelineId);
        Assert.Equal(2, state.Rows.Count);

        var art = state.Rows[0];
        Assert.Equal("Art", art.Path);
        Assert.Equal(string.Empty, art.ParentPath);
        Assert.Equal(SwfCharacterKind.Sprite, art.Kind);
        Assert.True(art.HasRect);
        Assert.Equal(new SwfBoundsRecord(10, 5, 40, 20), art.Rect);

        var field = state.Rows[1];
        Assert.Equal("Art/Cell", field.Path);
        Assert.Equal("Art", field.ParentPath);
        Assert.Equal(SwfCharacterKind.EditText, field.Kind);
        Assert.Equal(2.0, field.WorldTransform.ScaleX);
        Assert.Equal(10.0, field.WorldTransform.TranslateX);
        Assert.Equal(new SwfBoundsRecord(10, 5, 40, 20), field.Rect);
        Assert.NotNull(field.Text);
        Assert.Equal((ushort?)3, field.Text!.FontId);
        Assert.Equal((double?)12.0, field.Text.FontHeight);
        Assert.Equal((uint?)0xFF112233u, field.Text.Color);
        Assert.Equal((byte?)2, field.Text.Align);
        Assert.True(field.Text.Multiline);
        Assert.True(field.Text.WordWrap);
        Assert.True(field.Text.AutoSize);
        Assert.Equal("v", field.Text.Variable);
        Assert.Equal("Hi", field.Text.Text);
    }

    // Constraint: the managed record must carry the font table and the clip frame span, and a font fact the tag does not hold must read as null.
    [Fact]
    public void ReadsTheFontTableAndTheFrameSpanOfASpriteRow()
    {
        var fontName = new List<byte>();
        fontName.AddRange(SwfFixtures.U16(3));
        fontName.AddRange(SwfFixtures.Z("Display"));
        fontName.AddRange(SwfFixtures.Z("(c) Maker"));
        var symbols = new List<byte>();
        symbols.AddRange(SwfFixtures.U16(1));
        symbols.AddRange(SwfFixtures.U16(3));
        symbols.AddRange(SwfFixtures.Z("FaceClass"));
        using var movie = SwfFile.FromBytes(Movie(
            SwfFixtures.Tag(75, Font3(3, "Face")),
            SwfFixtures.Tag(88, fontName.ToArray()),
            SwfFixtures.Tag(76, symbols.ToArray()),
            SwfFixtures.Tag(39, Sprite(14, 3,
                SwfFixtures.Tag(1, Array.Empty<byte>()),
                SwfFixtures.Tag(1, Array.Empty<byte>()),
                SwfFixtures.Tag(1, Array.Empty<byte>()))),
            SwfFixtures.Tag(26, Place(2, 14, Matrix(0x10000, 0, 0), "Art")),
            SwfFixtures.Tag(1, Array.Empty<byte>())));
        var layout = movie.Layout();
        var font = Assert.Single(layout.Fonts);
        Assert.Equal((ushort)3, font.FontId);
        Assert.Equal((byte)3, font.Version);
        Assert.Equal("Face", font.Name);
        Assert.Equal((bool?)true, font.Bold);
        Assert.Equal((bool?)false, font.Italic);
        Assert.Equal((byte?)1, font.Language);
        Assert.Equal((uint?)1, font.GlyphCount);
        Assert.True(font.HasOutlines);
        Assert.Equal((ushort?)800, font.Ascent);
        Assert.Equal((ushort?)200, font.Descent);
        Assert.Equal((short?)40, font.Leading);
        Assert.Equal((uint?)20480, font.UnitsPerEm);
        Assert.NotNull(font.Codes);
        Assert.Equal(new ushort[] { 0x41 }, font.Codes!);
        Assert.Equal("Display", font.FontName);
        Assert.Equal("(c) Maker", font.FontCopyright);
        Assert.Equal("FaceClass", font.FontClass);
        var art = Assert.Single(Assert.Single(layout.States).Rows);
        Assert.Equal(3u, art.ClipFrameCount);
        Assert.Equal(0u, art.ClipFirstFrame);
        Assert.Equal(2u, art.ClipLastFrame);
    }

    // Constraint: a font info name, style and language and a text font class must reach the managed record.
    [Fact]
    public void ReadsAFontInfoNameStyleAndLanguageAndATextFieldFontClass()
    {
        var font = new byte[] { 0x07, 0x00, 0x02, 0x00, 0x10, 0x14, 0x88, 0x9C, 0xC4, 0x66, 0xA3, 0x2C, 0x40, 0x00 };
        var info = new List<byte>();
        info.AddRange(SwfFixtures.U16(7));
        info.Add(3);
        info.AddRange(Encoding.ASCII.GetBytes("Old"));
        info.Add(0x07);
        info.Add(2);
        info.AddRange(SwfFixtures.U16(0x263A));
        var field = new List<byte>();
        field.AddRange(SwfFixtures.U16(12));
        var bits = new SwfBitWriter();
        bits.Rect(0, 400, 0, 200, 12);
        field.AddRange(bits.ToArray());
        field.Add(0x80);
        field.Add(0x80);
        field.AddRange(SwfFixtures.Z("TextFont"));
        field.AddRange(SwfFixtures.U16(240));
        field.AddRange(SwfFixtures.Z("v"));
        field.AddRange(SwfFixtures.Z("Hi"));
        using var movie = SwfFile.FromBytes(Movie(
            SwfFixtures.Tag(10, font),
            SwfFixtures.Tag(62, info.ToArray()),
            SwfFixtures.Tag(37, field.ToArray()),
            SwfFixtures.Tag(26, Place(1, 12, Matrix(0x10000, 0, 0), "Field")),
            SwfFixtures.Tag(1, Array.Empty<byte>())));
        var layout = movie.Layout();
        var legacy = Assert.Single(layout.Fonts);
        Assert.Equal((byte)1, legacy.Version);
        Assert.Equal("Old", legacy.Name);
        Assert.Equal((bool?)true, legacy.Bold);
        Assert.Equal((bool?)true, legacy.Italic);
        Assert.Equal((byte?)2, legacy.Language);
        Assert.Equal(new ushort[] { 0x263A }, legacy.Codes!);
        var text = Assert.Single(Assert.Single(layout.States).Rows).Text;
        Assert.NotNull(text);
        Assert.Null(text!.FontId);
        Assert.Equal("TextFont", text.FontClass);
        Assert.Equal((double?)12.0, text.FontHeight);
        Assert.Equal("Hi", text.Text);
    }

    // Regression: the JSON export or its hash depended on the handle or on the order of native reads.
    [Fact]
    public void TwoOpensOfTheSameBytesGiveTheSameJsonAndHash()
    {
        var bytes = NamedMovie();
        using var first = SwfFile.FromBytes(bytes);
        using var second = SwfFile.FromBytes(bytes);
        var a = first.Layout();
        var b = second.Layout();
        Assert.Equal(a.ToJson(), b.ToJson());
        Assert.Equal(a.Sha256(), b.Sha256());
        Assert.Contains("\"parentPath\":\"Art\"", a.ToJson());
    }

    // Regression: the scripted flag was not copied from the native row, so a jumping script looked authoritative.
    [Fact]
    public void AScriptThatJumpsMarksEveryRowAsScripted()
    {
        var jump = Movie(
            SwfFixtures.Tag(37, EditText()),
            SwfFixtures.Tag(43, SwfFixtures.Z("Open")),
            SwfFixtures.Tag(12, new byte[] { 0x81, 0x02, 0x00, 0x00, 0x00, 0x00 }),
            SwfFixtures.Tag(26, Place(1, 11, Matrix(0x10000, 0, 0), "Field")),
            SwfFixtures.Tag(1, Array.Empty<byte>()));
        using var movie = SwfFile.FromBytes(jump);
        var layout = movie.Layout();
        Assert.True(layout.Scripted);
        Assert.False(layout.HasAs3);
        Assert.True(layout.LegacyTimelineScript);
        Assert.All(layout.States[0].Rows, row => Assert.True(row.Scripted));
        Assert.All(layout.States[0].Rows, row => Assert.True(row.LegacyTimelineScript));
        Assert.All(layout.States[0].Rows, row => Assert.False(row.HasAs3));
    }

    // Regression: AS3 and AVM1 timeline script shared one flag, so a UI could not tell a program that owns the stage from a frame script.
    [Fact]
    public void AnAs3MovieIsFlaggedAs3OnlyAndALegacyJumpIsNot()
    {
        var as3 = Movie(
            SwfFixtures.Tag(37, EditText()),
            SwfFixtures.Tag(43, SwfFixtures.Z("Open")),
            SwfFixtures.Tag(72, new byte[] { 0x00 }),
            SwfFixtures.Tag(26, Place(1, 11, Matrix(0x10000, 0, 0), "Field")),
            SwfFixtures.Tag(1, Array.Empty<byte>()));
        using var movie = SwfFile.FromBytes(as3);
        var layout = movie.Layout();
        Assert.True(layout.Scripted);
        Assert.True(layout.HasAs3);
        Assert.False(layout.LegacyTimelineScript);
        Assert.All(layout.States[0].Rows, row => Assert.True(row.Scripted && row.HasAs3 && !row.LegacyTimelineScript));
    }

    // Regression: a movie whose display list fails returned an empty layout instead of throwing.
    [Fact]
    public void RefusesAMovieWhoseDisplayListDoesNotBuild()
    {
        var move = new List<byte> { 0x05 };
        move.AddRange(SwfFixtures.U16(3));
        move.AddRange(Matrix(0x10000, 0, 0));
        using var movie = SwfFile.FromBytes(Movie(
            SwfFixtures.Tag(26, move.ToArray()),
            SwfFixtures.Tag(1, Array.Empty<byte>())));
        Assert.ThrowsAny<Exception>(() => movie.Layout());
    }

    // Regression: the JSON bytes or the hash changed with the platform's line ending or a serializer option, so a cached layout stopped matching its hash.
    [Fact]
    public void ToJsonPinsItsBytesAndHashSoAPlatformOrOptionChangeIsDetected()
    {
        var row = new SwfLayoutRowRecord("Art", string.Empty, 2, 14, SwfCharacterKind.Sprite, true, true,
            new SwfBoundsRecord(10, 5, 40, 20), new SwfTransformRecord(1, 0, 0, 1, 10, 5), null, 3, 0, 2, false, false, false, false, null);
        var field = new SwfLayoutRowRecord("Art/Field", "Art", 1, 11, SwfCharacterKind.EditText, true, false,
            new SwfBoundsRecord(0, 0, 0, 0), new SwfTransformRecord(1, 0, 0, 1, 0, 0), null, 0, 0, 0, false, false, false, false,
            new SwfLayoutTextRecord(null, 12, null, null, null, null, null, null, false, false, false, false, false, string.Empty, string.Empty, "TextFont"));
        var state = new SwfLayoutStateRecord("Open", 0, 0, new[] { row, field });
        var fonts = new[]
        {
            new SwfLayoutFontRecord(3, 3, "Face", true, false, 1, 1, true, 800, 200, 40, 20480, new ushort[] { 65 }, "Display", "Maker", "FaceClass"),
            new SwfLayoutFontRecord(7, 1, string.Empty, null, null, null, 1, true, null, null, null, null, null, null, null, null),
        };
        var layout = new SwfLayoutRecord(new SwfBoundsRecord(0, 0, 550, 400), 12, false, false, false, fonts, new[] { state });
        const string expected = "{\"stage\":{\"x\":0,\"y\":0,\"width\":550,\"height\":400},\"frameRate\":12,\"scripted\":false,\"hasAs3\":false,\"legacyTimelineScript\":false,\"fonts\":[{\"fontId\":3,\"version\":3,\"name\":\"Face\",\"bold\":true,\"italic\":false,\"language\":1,\"glyphCount\":1,\"hasOutlines\":true,\"ascent\":800,\"descent\":200,\"leading\":40,\"unitsPerEm\":20480,\"codes\":[65],\"fontName\":\"Display\",\"fontCopyright\":\"Maker\",\"fontClass\":\"FaceClass\"},{\"fontId\":7,\"version\":1,\"name\":\"\",\"bold\":null,\"italic\":null,\"language\":null,\"glyphCount\":1,\"hasOutlines\":true,\"ascent\":null,\"descent\":null,\"leading\":null,\"unitsPerEm\":null,\"codes\":null,\"fontName\":null,\"fontCopyright\":null,\"fontClass\":null}],\"states\":[{\"label\":\"Open\",\"timelineId\":0,\"frame\":0,\"rows\":[{\"path\":\"Art\",\"parentPath\":\"\",\"depth\":2,\"characterId\":14,\"kind\":6,\"visible\":true,\"hasRect\":true,\"rect\":{\"x\":10,\"y\":5,\"width\":40,\"height\":20},\"worldTransform\":{\"scaleX\":1,\"rotateSkew0\":0,\"rotateSkew1\":0,\"scaleY\":1,\"translateX\":10,\"translateY\":5},\"clipDepth\":null,\"clipFrameCount\":3,\"clipFirstFrame\":0,\"clipLastFrame\":2,\"scripted\":false,\"hasAs3\":false,\"legacyTimelineScript\":false,\"duplicatePath\":false,\"text\":null},{\"path\":\"Art/Field\",\"parentPath\":\"Art\",\"depth\":1,\"characterId\":11,\"kind\":4,\"visible\":true,\"hasRect\":false,\"rect\":{\"x\":0,\"y\":0,\"width\":0,\"height\":0},\"worldTransform\":{\"scaleX\":1,\"rotateSkew0\":0,\"rotateSkew1\":0,\"scaleY\":1,\"translateX\":0,\"translateY\":0},\"clipDepth\":null,\"clipFrameCount\":0,\"clipFirstFrame\":0,\"clipLastFrame\":0,\"scripted\":false,\"hasAs3\":false,\"legacyTimelineScript\":false,\"duplicatePath\":false,\"text\":{\"fontId\":null,\"fontHeight\":12,\"color\":null,\"align\":null,\"leftMargin\":null,\"rightMargin\":null,\"indent\":null,\"leading\":null,\"multiline\":false,\"wordWrap\":false,\"autoSize\":false,\"html\":false,\"border\":false,\"variable\":\"\",\"text\":\"\",\"fontClass\":\"TextFont\"}}]}]}";
        Assert.Equal(expected, layout.ToJson());
        Assert.Equal("E6DB56ACE230A8918D904C9280F6F6B60A7F95E87A93FD1CEB19661DFCB6D327", layout.Sha256());
    }
}
