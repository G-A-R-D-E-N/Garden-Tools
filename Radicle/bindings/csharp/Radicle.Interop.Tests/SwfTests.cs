using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using Radicle.Interop;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public partial class SwfTests
{
    private sealed class BitWriter
    {
        private readonly List<bool> _bits = new();

        public void Push(uint value, int count)
        {
            for (var index = count - 1; index >= 0; --index) _bits.Add(((value >> index) & 1u) != 0);
        }

        public byte[] ToBytes()
        {
            while (_bits.Count % 8 != 0) _bits.Add(false);
            var bytes = new byte[_bits.Count / 8];
            for (var index = 0; index < _bits.Count; ++index)
            {
                if (_bits[index]) bytes[index / 8] |= (byte)(1 << (7 - (index % 8)));
            }
            return bytes;
        }
    }

    private static byte[] Rect(int xMin, int xMax, int yMin, int yMax)
    {
        const int bits = 15;
        var writer = new BitWriter();
        writer.Push(bits, 5);
        writer.Push((uint)xMin & 0x7FFFu, bits);
        writer.Push((uint)xMax & 0x7FFFu, bits);
        writer.Push((uint)yMin & 0x7FFFu, bits);
        writer.Push((uint)yMax & 0x7FFFu, bits);
        return writer.ToBytes();
    }

    private static byte[] Tag(ushort code, byte[] payload)
    {
        var bytes = new List<byte>();
        ushort header = (ushort)((code << 6) | (payload.Length & 0x3F));
        if (payload.Length >= 0x3F)
        {
            header = (ushort)((code << 6) | 0x3F);
            bytes.AddRange(BitConverter.GetBytes(header));
            bytes.AddRange(BitConverter.GetBytes((uint)payload.Length));
        }
        else
        {
            bytes.AddRange(BitConverter.GetBytes(header));
        }
        bytes.AddRange(payload);
        return bytes.ToArray();
    }

    // An FWS movie whose tag walk, label decode and payload copy must need no game data.
    private static byte[] Movie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(69, BitConverter.GetBytes(0x79u)));
        body.AddRange(Tag(43, "start\0"u8.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ReadsTheTagTreeAndFrameLabelsAndCopiesPayloads()
    {
        using var swf = SwfFile.FromBytes(Movie());

        Assert.Equal((byte)10, swf.Info.Version);
        Assert.Equal((ushort)1, swf.Info.FrameCount);
        Assert.Equal(12.0f, swf.Info.FrameRate);
        Assert.True(swf.Info.ActionScript3);
        Assert.True(swf.Info.UseGpu);
        Assert.Equal(3, swf.Info.TagCount);

        Assert.Collection(
            swf.Tags,
            tag =>
            {
                Assert.Equal((ushort)69, tag.Code);
                Assert.Equal("FileAttributes", tag.Name);
                Assert.Equal(-1, tag.Parent);
                Assert.False(tag.InsideSprite);
            },
            tag => Assert.Equal((ushort)43, tag.Code),
            tag => Assert.Equal((ushort)0, tag.Code));

        Assert.Equal(new byte[] { 0x79, 0x00, 0x00, 0x00 }, swf.Payload(0));
        Assert.Single(swf.FrameLabels);
        Assert.Equal("start", swf.FrameLabels[0].Name);
        Assert.Equal(1, swf.FrameLabels[0].TagIndex);
        Assert.Empty(swf.Symbols);
        Assert.Empty(swf.Exports);
        Assert.Empty(swf.AbcBlocks);
        Assert.Empty(swf.Morphs);
        Assert.Empty(swf.Shapes);
        Assert.Empty(swf.Bitmaps);
        Assert.Empty(swf.Jpegs);
        Assert.Empty(swf.JpegTables);
    }

    private static byte[] MorphMovie()
    {
        var styles = new List<byte> { 1, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0x10 };
        var payload = new List<byte>();
        payload.AddRange(BitConverter.GetBytes((ushort)55));
        payload.AddRange(Rect(0, 100, 0, 80));
        payload.AddRange(Rect(0, 120, 0, 90));
        payload.AddRange(BitConverter.GetBytes((uint)(styles.Count + 1)));
        payload.AddRange(styles);
        payload.Add(0);
        payload.Add(0x10);
        payload.Add(0);

        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(46, payload.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public unsafe void ReadsAMorphFillPairThroughTheNativeGetters()
    {
        var movie = MorphMovie();
        IntPtr swf;
        fixed (byte* bytes = movie)
        {
            Assert.Equal(Status.Ok, RadicleNative.SwfOpenBytes(bytes, (nuint)movie.Length, &swf));
        }
        try
        {
            SwfMorphShape morph;
            Assert.Equal(Status.Ok, RadicleNative.SwfMorphAt(swf, 0, &morph));
            Assert.Equal((ushort)55, morph.CharacterId);
            Assert.Equal(1, (int)morph.FillStyleCount);
            Assert.Equal(0, (int)morph.LineStyleCount);

            SwfMorphFillStyle fill;
            Assert.Equal(Status.Ok, RadicleNative.SwfMorphFillAt(swf, 0, 0, &fill));
            Assert.Equal((byte)0x00, fill.Kind);
            Assert.Equal(0x04010203u, fill.StartColor);
            Assert.Equal(0x08050607u, fill.EndColor);
            Assert.Equal(0, fill.HasStartMatrix);
            Assert.Equal((nuint)0, fill.StopCount);

            Assert.Equal(Status.InvalidArgument, RadicleNative.SwfMorphFillAt(swf, 0, 1, &fill));
        }
        finally
        {
            RadicleNative.SwfDestroy(swf);
        }
    }

    private static byte[] MorphLineFillMovie()
    {
        var styles = new List<byte> { 0, 1 };
        styles.AddRange(BitConverter.GetBytes((ushort)40));
        styles.AddRange(BitConverter.GetBytes((ushort)80));
        styles.Add(0x08);
        styles.Add(0x00);
        styles.Add(0x10);
        var matrix = new BitWriter();
        matrix.Push(0, 1);
        matrix.Push(0, 1);
        matrix.Push(5, 5);
        matrix.Push(0, 5);
        matrix.Push(0, 5);
        styles.AddRange(matrix.ToBytes());
        styles.AddRange(matrix.ToBytes());
        styles.Add(0x01);
        styles.Add(90);
        styles.AddRange(new byte[] { 0x40, 0x41, 0x42, 0x43 });
        styles.Add(100);
        styles.AddRange(new byte[] { 0x44, 0x45, 0x46, 0x47 });

        var records = new BitWriter();
        records.Push(0, 1);
        records.Push(0x01, 5);
        records.Push(8, 5);
        records.Push(1, 8);
        records.Push(2, 8);
        records.Push(1, 1);
        records.Push(1, 1);
        records.Push(2, 4);
        records.Push(1, 1);
        records.Push(4, 4);
        records.Push(6, 4);
        records.Push(0, 6);
        var start = new List<byte>();
        start.AddRange(styles);
        start.Add(0x01);
        start.AddRange(records.ToBytes());

        var tail = new BitWriter();
        tail.Push(0, 1);
        tail.Push(0, 6);
        var end = new List<byte> { 0x01 };
        end.AddRange(tail.ToBytes());

        var payload = new List<byte>();
        payload.AddRange(BitConverter.GetBytes((ushort)56));
        payload.AddRange(Rect(0, 100, 0, 80));
        payload.AddRange(Rect(0, 120, 0, 90));
        payload.AddRange(Rect(0, 90, 0, 70));
        payload.AddRange(Rect(0, 110, 0, 80));
        payload.Add(0);
        payload.AddRange(BitConverter.GetBytes((uint)start.Count));
        payload.AddRange(start);
        payload.AddRange(end);

        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(84, payload.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ReadsMorphStylePairsThroughTheManagedWrapper()
    {
        using var swf = SwfFile.FromBytes(MorphMovie());

        var morph = Assert.Single(swf.Morphs);
        Assert.Equal(0, morph.TagIndex);
        Assert.Equal((ushort)55, morph.CharacterId);
        Assert.Equal((byte)1, morph.Version);
        Assert.False(morph.HasEdgeBounds);
        Assert.Equal(100, morph.StartBounds.XMax);
        Assert.Equal(120, morph.EndBounds.XMax);
        Assert.Empty(morph.Lines);
        Assert.Empty(morph.StartCommands);
        Assert.Empty(morph.EndCommands);

        var fill = Assert.Single(morph.Fills);
        Assert.Equal((byte)0x00, fill.Kind);
        Assert.Equal(0x04010203u, fill.StartColor);
        Assert.Equal(0x08050607u, fill.EndColor);
        Assert.Null(fill.StartMatrix);
        Assert.Null(fill.EndMatrix);
        Assert.Null(fill.StartFocalPoint);
        Assert.Empty(fill.Stops);
    }

    [Fact]
    public void ReadsAMorphLineFillThroughTheManagedWrapper()
    {
        using var swf = SwfFile.FromBytes(MorphLineFillMovie());

        var morph = Assert.Single(swf.Morphs);
        Assert.Equal((byte)2, morph.Version);
        Assert.True(morph.HasEdgeBounds);
        Assert.Empty(morph.Fills);

        var line = Assert.Single(morph.Lines);
        Assert.Equal((ushort)40, line.StartWidth);
        Assert.Equal((ushort)80, line.EndWidth);
        Assert.Equal((ushort)0x0800, line.Flags);
        Assert.Null(line.StartMiterLimit);
        Assert.Null(line.EndMiterLimit);
        Assert.Equal(0u, line.StartColor);

        Assert.Equal(2, morph.StartCommands.Count);
        Assert.Equal(SwfShapeCommandKind.Move, morph.StartCommands[0].Kind);
        Assert.Equal(1, morph.StartCommands[0].X);
        Assert.Equal(2, morph.StartCommands[0].Y);
        Assert.Equal(SwfShapeCommandKind.StraightEdge, morph.StartCommands[1].Kind);
        Assert.Equal(4, morph.StartCommands[1].X);
        Assert.Equal(6, morph.StartCommands[1].Y);
        Assert.Empty(morph.EndCommands);

        Assert.NotNull(line.Fill);
        var fill = line.Fill!;
        Assert.Equal((byte)0x10, fill.Kind);
        Assert.NotNull(fill.StartMatrix);
        Assert.NotNull(fill.EndMatrix);
        Assert.Equal(0x10000, fill.StartMatrix!.ScaleX);
        Assert.Equal(0u, fill.StartColor);

        var stop = Assert.Single(fill.Stops);
        Assert.Equal((byte)90, stop.StartRatio);
        Assert.Equal(0x43404142u, stop.StartColor);
        Assert.Equal((byte)100, stop.EndRatio);
        Assert.Equal(0x47444546u, stop.EndColor);
    }

    private static byte[] MorphTweenMovie()
    {
        var startRecords = new BitWriter();
        startRecords.Push(0, 1);
        startRecords.Push(0x01, 5);
        startRecords.Push(8, 5);
        startRecords.Push(1, 8);
        startRecords.Push(2, 8);
        startRecords.Push(1, 1);
        startRecords.Push(1, 1);
        startRecords.Push(2, 4);
        startRecords.Push(1, 1);
        startRecords.Push(4, 4);
        startRecords.Push(6, 4);
        startRecords.Push(0, 6);

        var endRecords = new BitWriter();
        endRecords.Push(0, 1);
        endRecords.Push(0x01, 5);
        endRecords.Push(8, 5);
        endRecords.Push(0xFF, 8);
        endRecords.Push(0xFE, 8);
        endRecords.Push(1, 1);
        endRecords.Push(1, 1);
        endRecords.Push(2, 4);
        endRecords.Push(1, 1);
        endRecords.Push(0xC, 4);
        endRecords.Push(0xA, 4);
        endRecords.Push(0, 6);

        var start = new List<byte> { 0, 0, 0 };
        start.AddRange(startRecords.ToBytes());

        var payload = new List<byte>();
        payload.AddRange(BitConverter.GetBytes((ushort)57));
        payload.AddRange(Rect(0, 100, 0, 80));
        payload.AddRange(Rect(0, 120, 0, 90));
        payload.AddRange(BitConverter.GetBytes((uint)start.Count));
        payload.AddRange(start);
        payload.Add(0);
        payload.AddRange(endRecords.ToBytes());

        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(46, payload.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void InterpolatesAMorphFrameThroughTheManagedWrapper()
    {
        using var swf = SwfFile.FromBytes(MorphTweenMovie());

        var morph = Assert.Single(swf.Morphs);
        Assert.Equal(4, morph.StartCommands[1].X);
        Assert.Equal(6, morph.StartCommands[1].Y);
        Assert.Equal(-4, morph.EndCommands[1].X);
        Assert.Equal(-6, morph.EndCommands[1].Y);
        var halfway = swf.MorphCommandsAt(0, 0.5);
        Assert.Equal(2, halfway.Count);
        Assert.Equal(SwfShapeCommandKind.Move, halfway[0].Kind);
        Assert.Equal(0, halfway[0].X);
        Assert.Equal(0, halfway[0].Y);
        Assert.Equal(SwfShapeCommandKind.StraightEdge, halfway[1].Kind);
        Assert.Equal(0, halfway[1].X);
        Assert.Equal(0, halfway[1].Y);

        var start = swf.MorphCommandsAt(0, -1.0);
        Assert.Equal(4, start[1].X);
        Assert.Equal(6, start[1].Y);
        var end = swf.MorphCommandsAt(0, 2.0);
        Assert.Equal(-4, end[1].X);
        Assert.Equal(-6, end[1].Y);

        var notMorph = Assert.Throws<RadicleException>(() => swf.MorphCommandsAt(1, 0.5));
        Assert.Equal(Status.NotFound, notMorph.Status);
        var outside = Assert.Throws<RadicleException>(() => swf.MorphCommandsAt(99, 0.5));
        Assert.Equal(Status.InvalidArgument, outside.Status);
    }

    private static byte[] Zlib(byte[] raw)
    {
        using var output = new MemoryStream();
        using (var stream = new ZLibStream(output, CompressionLevel.Optimal, leaveOpen: true))
        {
            stream.Write(raw);
        }
        return output.ToArray();
    }

    private static byte[] ArtMovie()
    {
        var shape = new List<byte>();
        shape.AddRange(BitConverter.GetBytes((ushort)44));
        shape.AddRange(Rect(0, 200, 0, 100));
        shape.AddRange(Rect(0, 190, 0, 90));
        shape.Add(0x00);
        shape.Add(1);
        shape.Add(0x00);
        shape.AddRange(new byte[] { 0x11, 0x22, 0x33, 0x44 });
        shape.Add(2);
        shape.AddRange(BitConverter.GetBytes((ushort)20));
        shape.Add(0x20);
        shape.Add(0x00);
        shape.AddRange(BitConverter.GetBytes((ushort)0x0300));
        shape.AddRange(new byte[] { 0x55, 0x66, 0x77, 0x88 });
        shape.AddRange(BitConverter.GetBytes((ushort)5));
        shape.Add(0x08);
        shape.Add(0x00);
        shape.Add(0x10);
        var gradient = new BitWriter();
        gradient.Push(0, 1);
        gradient.Push(0, 1);
        gradient.Push(5, 5);
        gradient.Push(0, 5);
        gradient.Push(0, 5);
        shape.AddRange(gradient.ToBytes());
        shape.Add(0x01);
        shape.Add(90);
        shape.AddRange(new byte[] { 0x40, 0x41, 0x42, 0x43 });
        shape.Add(0x12);
        var records = new BitWriter();
        records.Push(0, 1);
        records.Push(0x01, 5);
        records.Push(8, 5);
        records.Push(1, 8);
        records.Push(2, 8);
        records.Push(1, 1);
        records.Push(1, 1);
        records.Push(2, 4);
        records.Push(1, 1);
        records.Push(4, 4);
        records.Push(6, 4);
        records.Push(0, 6);
        shape.AddRange(records.ToBytes());

        var bitmap = new List<byte>();
        bitmap.AddRange(BitConverter.GetBytes((ushort)46));
        bitmap.Add(5);
        bitmap.AddRange(BitConverter.GetBytes((ushort)2));
        bitmap.AddRange(BitConverter.GetBytes((ushort)2));
        bitmap.AddRange(Zlib(new byte[16]));

        var image = new byte[] { 0xFF, 0xD8, 0xFF, 0xD9 };
        var jpeg = new List<byte>();
        jpeg.AddRange(BitConverter.GetBytes((ushort)47));
        jpeg.AddRange(image);

        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(83, shape.ToArray()));
        body.AddRange(Tag(36, bitmap.ToArray()));
        body.AddRange(Tag(6, jpeg.ToArray()));
        body.AddRange(Tag(8, image));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ReadsShapeBitmapAndJpegTagsThroughTheManagedWrapper()
    {
        using var swf = SwfFile.FromBytes(ArtMovie());

        var shape = Assert.Single(swf.Shapes);
        Assert.Equal(0, shape.TagIndex);
        Assert.Equal((ushort)44, shape.CharacterId);
        Assert.Equal((byte)4, shape.Version);
        Assert.True(shape.HasEdgeBounds);
        Assert.Equal(200, shape.Bounds.XMax);
        Assert.Equal(190, shape.EdgeBounds.XMax);
        Assert.Equal(1u, shape.EdgeCount);
        Assert.Equal(0u, shape.CurvedEdgeCount);
        Assert.Equal(1u, shape.StyleChangeCount);

        Assert.Equal(2, shape.Commands.Count);
        Assert.Equal(SwfShapeCommandKind.Move, shape.Commands[0].Kind);
        Assert.Equal(1, shape.Commands[0].X);
        Assert.Equal(2, shape.Commands[0].Y);
        Assert.Equal(SwfShapeCommandKind.StraightEdge, shape.Commands[1].Kind);
        Assert.Equal(4, shape.Commands[1].X);
        Assert.Equal(6, shape.Commands[1].Y);

        var fill = Assert.Single(shape.Fills);
        Assert.Equal((byte)0x00, fill.Kind);
        Assert.Equal(0x44112233u, fill.Color);
        Assert.Null(fill.Matrix);
        Assert.Null(fill.FocalPoint);
        Assert.Empty(fill.Stops);

        Assert.Equal(2, shape.Lines.Count);
        var miter = shape.Lines[0];
        Assert.Equal((ushort)20, miter.Width);
        Assert.Equal((ushort)0x2000, miter.Flags);
        Assert.Equal(0x88556677u, miter.Color);
        Assert.Equal((ushort)0x0300, miter.MiterLimit);
        Assert.Null(miter.Fill);

        var filled = shape.Lines[1];
        Assert.Equal((ushort)5, filled.Width);
        Assert.Equal((ushort)0x0800, filled.Flags);
        Assert.Equal(0u, filled.Color);
        Assert.Null(filled.MiterLimit);
        Assert.NotNull(filled.Fill);
        var stroke = filled.Fill!;
        Assert.Equal((byte)0x10, stroke.Kind);
        Assert.NotNull(stroke.Matrix);
        Assert.Equal(0x10000, stroke.Matrix!.ScaleX);
        var strokeStop = Assert.Single(stroke.Stops);
        Assert.Equal((byte)90, strokeStop.Ratio);
        Assert.Equal(0x43404142u, strokeStop.Color);

        var bitmap = Assert.Single(swf.Bitmaps);
        Assert.Equal(1, bitmap.TagIndex);
        Assert.Equal((ushort)46, bitmap.CharacterId);
        Assert.Equal((byte)5, bitmap.Format);
        Assert.Equal((ushort)2, bitmap.Width);
        Assert.Equal((ushort)2, bitmap.Height);
        Assert.Null(bitmap.ColorTableSize);
        Assert.Equal((long)Zlib(new byte[16]).Length, bitmap.DataSize);

        var jpeg = Assert.Single(swf.Jpegs);
        Assert.Equal(2, jpeg.TagIndex);
        Assert.Equal((ushort)47, jpeg.CharacterId);
        Assert.Equal((ushort)0, jpeg.Deblock);
        Assert.Equal(4L, jpeg.ImageSize);
        Assert.Null(jpeg.AlphaSize);

        var tables = Assert.Single(swf.JpegTables);
        Assert.Equal(3, tables.TagIndex);
        Assert.Equal(4L, tables.Size);
    }

    private static byte[] CommandMovie()
    {
        var shape = new List<byte>();
        shape.AddRange(BitConverter.GetBytes((ushort)57));
        shape.AddRange(Rect(0, 200, 0, 100));
        shape.Add(1);
        shape.Add(0x00);
        shape.AddRange(new byte[] { 0x11, 0x22, 0x33, 0x44 });
        shape.Add(0);
        shape.Add(0x20);
        var records = new BitWriter();
        records.Push(0, 1);
        records.Push(0x03, 5);
        records.Push(4, 5);
        records.Push(3, 4);
        records.Push(0x0Eu, 4);
        records.Push(1, 2);
        records.Push(1, 1);
        records.Push(0, 1);
        records.Push(2, 4);
        records.Push(5, 4);
        records.Push(6, 4);
        records.Push(7, 4);
        records.Push(0, 4);
        records.Push(0, 6);
        shape.AddRange(records.ToBytes());

        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(32, shape.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ReadsMovesStyleChangesAndCurvedEdgesInOrder()
    {
        using var swf = SwfFile.FromBytes(CommandMovie());

        var shape = Assert.Single(swf.Shapes);
        Assert.Equal(3, shape.Commands.Count);

        var move = shape.Commands[0];
        Assert.Equal(SwfShapeCommandKind.Move, move.Kind);
        Assert.Equal(3, move.X);
        Assert.Equal(-2, move.Y);

        var style = shape.Commands[1];
        Assert.Equal(SwfShapeCommandKind.StyleChange, style.Kind);
        Assert.Equal((ushort)1, style.Fill0);
        Assert.Equal((ushort)0, style.Fill1);
        Assert.Equal((ushort)0, style.Line);

        var curve = shape.Commands[2];
        Assert.Equal(SwfShapeCommandKind.CurvedEdge, curve.Kind);
        Assert.Equal(5, curve.ControlX);
        Assert.Equal(6, curve.ControlY);
        Assert.Equal(7, curve.X);
        Assert.Equal(0, curve.Y);
    }

    [Fact]
    public void RefusesBytesThatAreNotAMovieAndAHandleThatIsDisposed()
    {
        Assert.Throws<RadicleException>(() => SwfFile.FromBytes(new byte[] { 0x5A, 0x57, 0x53, 13, 0, 0, 0, 0 }));

        var swf = SwfFile.FromBytes(Movie());
        swf.Dispose();
        Assert.Throws<ObjectDisposedException>(() => swf.Payload(0));
    }

    [Fact]
    public void InspectTextRendersTheRootLabelsOfTheMovie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(43, "start\0"u8.ToArray()));
        body.AddRange(Tag(1, Array.Empty<byte>()));
        body.AddRange(Tag(0, Array.Empty<byte>()));
        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        using var swf = SwfFile.FromBytes(file.ToArray());

        var text = swf.InspectText("movie.swf");

        Assert.StartsWith("## movie.swf\nStage ", text);
        Assert.Contains("Root frame labels: start@1\n", text);
    }

    [Fact]
    public void RefusesAnAvm1DisassemblyOfANonActionTag()
    {
        using var swf = SwfFile.FromBytes(Movie());

        var refused = Assert.Throws<RadicleException>(() => swf.Avm1Disassemble(0));
        Assert.Equal(Status.InvalidArgument, refused.Status);
    }

    [Fact]
    public void ExportsTheRootFrameAsSvgThroughTheManagedWrapper()
    {
        using var swf = SwfFile.FromBytes(RedSquareMovie());

        var svg = swf.FrameSvg(0);

        Assert.StartsWith("<svg", svg);
        Assert.Contains("<path d=\"M0 0L50 0L50 50L0 50L0 0Z\"", svg);
    }

    private static byte[] RedSquareMovie()
    {
        var shape = new List<byte>();
        shape.AddRange(BitConverter.GetBytes((ushort)1));
        shape.AddRange(Rect(0, 1000, 0, 1000));
        shape.Add(1);
        shape.Add(0x00);
        shape.AddRange(new byte[] { 0xFF, 0x00, 0x00, 0xFF });
        shape.Add(0);
        shape.Add(0x10);
        var edges = new BitWriter();
        edges.Push(0, 1);
        edges.Push(0x03, 5);
        edges.Push(12, 5);
        edges.Push(0, 12);
        edges.Push(0, 12);
        edges.Push(1, 1);
        int[] deltas = { 1000, 0, 0, 1000, -1000, 0, 0, -1000 };
        for (var index = 0; index < deltas.Length; index += 2)
        {
            edges.Push(1, 1);
            edges.Push(1, 1);
            edges.Push(10, 4);
            edges.Push(1, 1);
            edges.Push((uint)deltas[index] & 0xFFFu, 12);
            edges.Push((uint)deltas[index + 1] & 0xFFFu, 12);
        }
        edges.Push(0, 6);
        shape.AddRange(edges.ToBytes());

        var place = new List<byte>();
        place.Add(0x06);
        place.AddRange(BitConverter.GetBytes((ushort)1));
        place.AddRange(BitConverter.GetBytes((ushort)1));
        place.Add(0x00);

        var body = new List<byte>();
        body.AddRange(Rect(0, 11000, 0, 8000));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(32, shape.ToArray()));
        body.AddRange(Tag(26, place.ToArray()));
        body.AddRange(Tag(1, Array.Empty<byte>()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }
}
