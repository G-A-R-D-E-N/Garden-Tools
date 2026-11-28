using System;
using System.Collections.Generic;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed class NifAnimControlled
{
    public required string NodeName { get; init; }
    public required string ControllerType { get; init; }
    public required string InterpolatorId { get; init; }
    public required int InterpolatorRef { get; init; }
    public required int ControllerRef { get; init; }
    public required byte Priority { get; init; }
}

public sealed class NifAnimSequenceInfo
{
    public required int Block { get; init; }
    public required string Name { get; init; }
    public required float Weight { get; init; }
    public required int TextKeysRef { get; init; }
    public required uint Cycle { get; init; }
    public required float Frequency { get; init; }
    public required float StartTime { get; init; }
    public required float StopTime { get; init; }
    public required int ManagerRef { get; init; }
    public required string AccumRoot { get; init; }
    public required NifAnimControlled[] Controlled { get; init; }
}

public sealed class NifAnimTextKeyInfo
{
    public required float Time { get; init; }
    public required string Text { get; init; }
}

public sealed class NifAnimGraphInfo
{
    public required int Block { get; init; }
    public required string GraphFile { get; init; }
    public required bool ControlsBaseSkeleton { get; init; }
}

public sealed class NifAnimPoseInfo
{
    public required string NodeName { get; init; }
    public required System.Numerics.Vector3 Translation { get; init; }
    public required System.Numerics.Vector4 Rotation { get; init; }
    public required float Scale { get; init; }
}

public sealed class NifAnimManagerInfo
{
    public required int Block { get; init; }
    public required int Cumulative { get; init; }
    public required int[] SequenceBlocks { get; init; }
}

public sealed class NifAnimValueKeyFrame
{
    public required float Time { get; init; }
    public required System.Numerics.Vector3 Value { get; init; }
    public required System.Numerics.Vector3 Forward { get; init; }
    public required System.Numerics.Vector3 Backward { get; init; }
    public required System.Numerics.Vector3 Tbc { get; init; }
}

public sealed class NifAnimValueKeys
{
    public required int Components { get; init; }
    public required uint Interpolation { get; init; }
    public required int DataRef { get; init; }
    public required System.Numerics.Vector3 Pose { get; init; }
    public required NifAnimValueKeyFrame[] Keys { get; init; }
}

public sealed class NifAnimFile : IDisposable
{
    private IntPtr _handle;
    private bool _disposed;

    private NifAnimFile(IntPtr handle) { _handle = handle; }

    public static unsafe NifAnimFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a NIF needs at least a header", nameof(bytes));
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.NifAnimRead(pointer, (nuint)bytes.Length, &handle));
        }
        return new NifAnimFile(handle);
    }

    public void Dispose()
    {
        if (!_disposed) { RadicleNative.NifAnimDestroy(_handle); _disposed = true; }
        GC.SuppressFinalize(this);
    }

    ~NifAnimFile() { if (!_disposed) RadicleNative.NifAnimDestroy(_handle); }

    public unsafe IReadOnlyList<NifAnimSequenceInfo> Sequences
    {
        get
        {
            nuint count = 0;
            Call.Check(RadicleNative.NifAnimSequenceCount(_handle, &count));
            var outList = new List<NifAnimSequenceInfo>(checked((int)count));
            for (nuint i = 0; i < count; ++i) outList.Add(ReadSequence(i));
            return outList;
        }
    }

    public unsafe IReadOnlyList<int> SequenceBlocks
    {
        get
        {
            nuint count = 0;
            Call.Check(RadicleNative.NifAnimSequenceCount(_handle, &count));
            var outList = new int[checked((int)count)];
            for (nuint i = 0; i < count; ++i)
            {
                NifAnimSequence seq;
                Call.Check(RadicleNative.NifAnimReadSequence(_handle, i, &seq));
                outList[checked((int)i)] = checked((int)seq.Block);
            }
            return outList;
        }
    }

    public unsafe IReadOnlyList<NifAnimManagerInfo> Managers
    {
        get
        {
            nuint count = 0;
            Call.Check(RadicleNative.NifAnimManagerCount(_handle, &count));
            var outList = new List<NifAnimManagerInfo>(checked((int)count));
            for (nuint i = 0; i < count; ++i)
            {
                NifAnimManager manager;
                Call.Check(RadicleNative.NifAnimReadManager(_handle, i, &manager));
                var sequences = new int[checked((int)manager.SequenceCount)];
                var native = (uint*)manager.Sequences;
                for (var s = 0; s < sequences.Length; ++s) sequences[s] = checked((int)native[s]);
                outList.Add(new NifAnimManagerInfo {
                    Block = checked((int)manager.Block),
                    Cumulative = manager.Cumulative,
                    SequenceBlocks = sequences,
                });
            }
            return outList;
        }
    }

    public unsafe NifAnimValueKeys ReadValueKeys(int interpolatorBlock)
    {
        Native.NifAnimValueKeyInfo info;
        Call.Check(RadicleNative.NifAnimReadValueKeys(_handle, (uint)interpolatorBlock, &info));
        var keys = new NifAnimValueKeyFrame[checked((int)info.KeyCount)];
        for (var i = 0; i < keys.Length; ++i)
        {
            Native.NifAnimValueKey key;
            Call.Check(RadicleNative.NifAnimReadValueKey(_handle, (uint)interpolatorBlock, (nuint)i, &key));
            keys[i] = new NifAnimValueKeyFrame {
                Time = key.Time,
                Value = new System.Numerics.Vector3(key.Value[0], key.Value[1], key.Value[2]),
                Forward = new System.Numerics.Vector3(key.Forward[0], key.Forward[1], key.Forward[2]),
                Backward = new System.Numerics.Vector3(key.Backward[0], key.Backward[1], key.Backward[2]),
                Tbc = new System.Numerics.Vector3(key.Tbc[0], key.Tbc[1], key.Tbc[2]),
            };
        }
        return new NifAnimValueKeys {
            Components = checked((int)info.Components),
            Interpolation = info.Interpolation,
            DataRef = info.DataRef,
            Pose = new System.Numerics.Vector3(info.Pose[0], info.Pose[1], info.Pose[2]),
            Keys = keys,
        };
    }

    private unsafe NifAnimSequenceInfo ReadSequence(nuint index)
    {
        NifAnimSequence seq;
        Call.Check(RadicleNative.NifAnimReadSequence(_handle, index, &seq));
        byte* name = null; nuint nameSize = 0;
        Call.Check(RadicleNative.NifAnimSequenceName(_handle, index, &name, &nameSize));
        byte* accum = null; nuint accumSize = 0;
        Call.Check(RadicleNative.NifAnimSequenceAccumRoot(_handle, index, &accum, &accumSize));
        nuint controlledCount = 0;
        Call.Check(RadicleNative.NifAnimControlledCount(_handle, index, &controlledCount));
        var controlled = new NifAnimControlled[checked((int)controlledCount)];
        for (nuint s = 0; s < controlledCount; ++s) controlled[checked((int)s)] = ReadControlled(index, s);
        return new NifAnimSequenceInfo {
            Block = checked((int)seq.Block),
            Name = Call.TakeLatin1(name, nameSize),
            Weight = seq.Weight,
            TextKeysRef = seq.TextKeysRef,
            Cycle = seq.Cycle,
            Frequency = seq.Frequency,
            StartTime = seq.StartTime,
            StopTime = seq.StopTime,
            ManagerRef = seq.ManagerRef,
            AccumRoot = Call.TakeLatin1(accum, accumSize),
            Controlled = controlled,
        };
    }

    private unsafe NifAnimControlled ReadControlled(nuint index, nuint slot)
    {
        Native.NifAnimControlled entry;
        Call.Check(RadicleNative.NifAnimReadControlled(_handle, index, slot, &entry));
        byte* node = null; nuint nodeSize = 0;
        Call.Check(RadicleNative.NifAnimControlledNode(_handle, index, slot, &node, &nodeSize));
        byte* ctype = null; nuint ctypeSize = 0;
        Call.Check(RadicleNative.NifAnimControlledControllerType(_handle, index, slot, &ctype, &ctypeSize));
        byte* iid = null; nuint iidSize = 0;
        Call.Check(RadicleNative.NifAnimControlledInterpolatorId(_handle, index, slot, &iid, &iidSize));
        return new NifAnimControlled {
            NodeName = Call.TakeLatin1(node, nodeSize),
            ControllerType = Call.TakeLatin1(ctype, ctypeSize),
            InterpolatorId = Call.TakeLatin1(iid, iidSize),
            InterpolatorRef = entry.InterpolatorRef,
            ControllerRef = entry.ControllerRef,
            Priority = entry.Priority,
        };
    }

    public unsafe IReadOnlyList<NifAnimTextKeyInfo> ReadTextKeys(int block)
    {
        nuint count = 0;
        Call.Check(RadicleNative.NifAnimTextKeyCount(_handle, (uint)block, &count));
        var outList = new List<NifAnimTextKeyInfo>(checked((int)count));
        for (nuint i = 0; i < count; ++i)
        {
            NifAnimTextKey key;
            Call.Check(RadicleNative.NifAnimReadTextKey(_handle, (uint)block, i, &key));
            byte* text = null; nuint textSize = 0;
            Call.Check(RadicleNative.NifAnimTextKeyText(_handle, (uint)block, i, &text, &textSize));
            outList.Add(new NifAnimTextKeyInfo { Time = key.Time, Text = Call.TakeLatin1(text, textSize) });
        }
        return outList;
    }

    public unsafe IReadOnlyList<NifAnimGraphInfo> Graphs
    {
        get
        {
            nuint count = 0;
            Call.Check(RadicleNative.NifAnimGraphCount(_handle, &count));
            var outList = new List<NifAnimGraphInfo>(checked((int)count));
            for (nuint i = 0; i < count; ++i)
            {
                NifAnimGraph graph;
                Call.Check(RadicleNative.NifAnimReadGraph(_handle, i, &graph));
                byte* file = null; nuint fileSize = 0;
                Call.Check(RadicleNative.NifAnimGraphFile(_handle, i, &file, &fileSize));
                outList.Add(new NifAnimGraphInfo {
                    Block = checked((int)graph.Block),
                    GraphFile = Call.TakeLatin1(file, fileSize),
                    ControlsBaseSkeleton = graph.ControlsBaseSkeleton != 0,
                });
            }
            return outList;
        }
    }

    public unsafe IReadOnlyList<NifAnimPoseInfo> Evaluate(int sequenceBlock, float time)
    {
        nuint count = 0;
        Call.Check(RadicleNative.NifAnimEvaluateCount(_handle, (uint)sequenceBlock, time, &count));
        var outList = new List<NifAnimPoseInfo>(checked((int)count));
        for (nuint i = 0; i < count; ++i)
        {
            byte* node = null; nuint nodeSize = 0;
            Call.Check(RadicleNative.NifAnimEvaluateNode(_handle, (uint)sequenceBlock, time, i, &node, &nodeSize));
            NifAnimPose pose;
            Call.Check(RadicleNative.NifAnimEvaluatePose(_handle, (uint)sequenceBlock, time, i, &pose));
            outList.Add(new NifAnimPoseInfo {
                NodeName = Call.TakeLatin1(node, nodeSize),
                Translation = new System.Numerics.Vector3(pose.Translation[0], pose.Translation[1], pose.Translation[2]),
                Rotation = new System.Numerics.Vector4(pose.Rotation[0], pose.Rotation[1], pose.Rotation[2], pose.Rotation[3]),
                Scale = pose.Scale,
            });
        }
        return outList;
    }
}
