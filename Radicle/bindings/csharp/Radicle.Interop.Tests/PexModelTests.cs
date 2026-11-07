using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class PexModelTests
{
    private static readonly byte[] HighBytes = { 0x80, 0xA9, 0xFE, 0xFF };

    internal static byte[] SyntheticPex(bool withCode = false, bool withHandlers = false, bool duplicateStrings = false, bool withoutParent = false)
    {
        var bytes = new List<byte>();
        void U8(byte value) => bytes.Add(value);
        void U16(ushort value)
        {
            U8((byte)(value & 0xFF));
            U8((byte)(value >> 8));
        }
        void U32(uint value)
        {
            U16((ushort)(value & 0xFFFF));
            U16((ushort)(value >> 16));
        }
        void I64(long value)
        {
            U32((uint)((ulong)value & 0xFFFFFFFF));
            U32((uint)((ulong)value >> 32));
        }
        void Text(string text)
        {
            U16((ushort)text.Length);
            foreach (var character in text) U8((byte)character);
        }

        U32(0xFA57C0DEu);
        U8(3);
        U8(9);
        U16(2);
        I64(0x0102030405060708L);

        Text(string.Empty);
        Text(string.Empty);
        Text(string.Empty);

        U16(duplicateStrings ? (ushort)6 : (ushort)5);
        Text(string.Empty);
        Text("SyntheticObject");
        Text("ObjectReference");
        Text("Int");
        U16((ushort)HighBytes.Length);
        foreach (var value in HighBytes) U8(value);
        if (duplicateStrings)
        {
            U16((ushort)HighBytes.Length);
            foreach (var value in HighBytes) U8(value);
        }

        U8(0);
        U16(0);
        U16(1);

        U16(1);
        var declaredSize = bytes.Count;
        U32(0);

        var bodyStart = bytes.Count;
        U16(withoutParent ? (ushort)0 : (ushort)2);
        U16(0);
        U8(0);
        U32(0);
        U16(0);

        U16(0);

        U16(1);
        U16(0);
        U16(3);
        U32(0);
        U8(3);
        U32(42);
        U8(0);

        U16(withHandlers ? (ushort)1 : (ushort)0);
        if (withHandlers)
        {
            U16(1);
            U16(3);
            U16(0);
            U32(0);
            U8(3);
            for (var handler = 0; handler < 2; ++handler)
            {
                U16(3);
                U16(0);
                U32(0);
                U8(0);
                U16((ushort)handler);
                if (handler == 1)
                {
                    U16(3);
                    U16(3);
                }
                U16(0);
                U16(0);
            }
        }

        U16(1);
        U16(0);
        U16(1);
        U16(0);
        U16(0);
        U16(0);
        U32(0);
        U8(0);
        if (withCode)
        {
            U16(0);
            U16(1);
            U16(3);
            U16(3);
            U16(2);
            U8(13);
            U8(1);
            U16(3);
            U8(3);
            U32(unchecked((uint)-7));
            U8(25);
            U8(1);
            U16(2);
            U8(1);
            U16(1);
            U8(1);
            U16(3);
            U8(3);
            U32(1);
            U8(2);
            U16(duplicateStrings ? (ushort)5 : (ushort)4);
        }
        else
        {
            U16(0);
            U16(0);
            U16(0);
        }

        var body = bytes.Count - bodyStart;
        var size = (uint)(body + sizeof(uint));
        bytes[declaredSize] = (byte)(size & 0xFF);
        bytes[declaredSize + 1] = (byte)((size >> 8) & 0xFF);
        bytes[declaredSize + 2] = (byte)((size >> 16) & 0xFF);
        bytes[declaredSize + 3] = (byte)((size >> 24) & 0xFF);
        return bytes.ToArray();
    }

    [Fact]
    public void ParsesTheSyntheticFixture()
    {
        using var pex = PexFile.FromBytes(SyntheticPex());

        Assert.Equal(3, pex.Header.MajorVersion);
        Assert.Equal(9, pex.Header.MinorVersion);
        Assert.Equal(2, pex.Header.GameId);
        Assert.Equal(0x0102030405060708L, pex.Header.CompilationTime);
        Assert.Equal(5, pex.Header.StringCount);
        Assert.Equal(1, pex.Header.ObjectCount);
        Assert.Single(pex.Objects);
    }

    [Fact]
    public void ReportsObjectNamesAndCounts()
    {
        using var pex = PexFile.FromBytes(SyntheticPex());
        var info = pex.Objects[0];

        Assert.Equal("SyntheticObject", Encoding.Latin1.GetString(info.Name));
        Assert.Equal("ObjectReference", Encoding.Latin1.GetString(info.ParentClassName));
        Assert.Equal(new PexObjectCounts(0, 1, 0, 1, 1), info.Counts);
    }

    [Fact]
    public void StringTableEntriesAreReturnedVerbatim()
    {
        using var pex = PexFile.FromBytes(SyntheticPex());

        Assert.Equal(5, pex.StringTable.Count);
        Assert.Equal(HighBytes, pex.StringTable[4]);
        Assert.Empty(pex.StringTable[0]);
    }

    [Fact]
    public void RoundTripsThroughToBytes()
    {
        var bytes = SyntheticPex();
        using var pex = PexFile.FromBytes(bytes);

        Assert.Equal(bytes, pex.ToBytes());
    }

    [Fact]
    public void ReadsFromAFile()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("fixture.pex", SyntheticPex());
        using var pex = PexFile.FromFile(path);
        Assert.Equal(1, pex.Header.ObjectCount);
        Assert.Equal(SyntheticPex(), pex.ToBytes());
    }

    [Fact]
    public void TheSnapshotSurvivesDispose()
    {
        var pex = PexFile.FromBytes(SyntheticPex());
        var name = pex.Objects[0].Name;
        var table = pex.StringTable[4];
        pex.Dispose();

        Assert.Equal("SyntheticObject", Encoding.Latin1.GetString(name));
        Assert.Equal(HighBytes, table);
        Assert.Throws<ObjectDisposedException>(() => pex.ToBytes());
    }

    [Fact]
    public void MalformedBytesThrowAParseError()
    {
        var error = Assert.Throws<RadicleException>(() => PexFile.FromBytes(new byte[] { 0x00, 0x01, 0x02, 0x03 }));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.False(string.IsNullOrEmpty(error.Message));
    }

    [Fact]
    public void MissingFileThrowsAnIoError()
    {
        var error = Assert.Throws<RadicleException>(() =>
            PexFile.FromFile(Path.Combine(Path.GetTempPath(), "radicle_no_such_directory", "no_such_file.pex")));
        Assert.Equal(Status.IoError, error.Status);
    }

    [Fact]
    public void ReadsAnObjectsCodeAsStringTableIndexes()
    {
        using var pex = PexFile.FromBytes(SyntheticPex(withCode: true));
        var code = pex.ReadCode(0);

        var state = Assert.Single(code.States);
        Assert.Equal(0, state.Name);
        var function = code.Functions[Assert.Single(state.Functions)];
        Assert.Equal(0, function.State);
        Assert.Empty(function.Parameters);
        Assert.Equal(new PexTypedName(3, 3), Assert.Single(function.Locals));
        Assert.Equal(2, function.Instructions.Count);

        var assign = function.Instructions[0];
        Assert.Equal(13, assign.Opcode);
        Assert.Equal(new[] { new PexValue(PexValueType.Identifier, 3, 0, 0, false), new PexValue(PexValueType.Integer, -1, -7, 0, false) },
            assign.Arguments);

        var call = function.Instructions[1];
        Assert.Equal(25, call.Opcode);
        Assert.Equal(new[] { 2, 1, 3, 4 }, call.Arguments.Select(argument => argument.String));
        Assert.Equal(PexValueType.String, call.Arguments[3].Type);
        Assert.Equal(HighBytes, pex.StringTable[call.Arguments[3].String]);

        var variable = Assert.Single(code.Variables);
        Assert.Equal(new PexValue(PexValueType.Integer, -1, 42, 0, false), variable.DefaultValue);
        Assert.Empty(code.Properties);
        Assert.Empty(code.Structs);
    }

    [Fact]
    public void ReadCodeRefusesAnUnknownObjectAndADisposedFile()
    {
        var pex = PexFile.FromBytes(SyntheticPex());
        var error = Assert.Throws<RadicleException>(() => pex.ReadCode(1));
        Assert.Equal(Status.InvalidArgument, error.Status);
        pex.Dispose();
        Assert.Throws<ObjectDisposedException>(() => pex.ReadCode(0));
    }

    [Fact]
    public void SnapshotFunctionCountExcludesBothPropertyHandlers()
    {
        using var pex = PexFile.FromBytes(SyntheticPex(withHandlers: true));
        var code = pex.ReadCode(0);
        Assert.Equal(1, pex.Objects[0].Counts.FunctionCount);
        Assert.Single(Assert.Single(code.States).Functions);
        Assert.Equal(3, code.Functions.Count);
        var property = Assert.Single(code.Properties);
        Assert.Equal(1, property.ReadFunction);
        Assert.Equal(2, property.WriteFunction);
        Assert.Equal(-1, code.Functions[property.ReadFunction].State);
        Assert.Equal(-1, code.Functions[property.WriteFunction].State);
        Assert.Single(code.Functions[property.WriteFunction].Parameters);
    }

    [Fact]
    public void DuplicateStringIndexesCanonicalizeWithoutChangingTheirBytes()
    {
        using var pex = PexFile.FromBytes(SyntheticPex(withCode: true, duplicateStrings: true));
        Assert.Equal(HighBytes, pex.StringTable[4]);
        Assert.Equal(HighBytes, pex.StringTable[5]);
        var operand = pex.ReadCode(0).Functions[0].Instructions[1].Arguments[3];
        Assert.Equal(PexValueType.String, operand.Type);
        Assert.Equal(4, operand.String);
        Assert.Equal(HighBytes, pex.StringTable[operand.String]);
    }
}
