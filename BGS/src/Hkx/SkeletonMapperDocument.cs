using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public sealed class SkeletonMapperNode
{
    internal SkeletonMapperNode(PackfileObjects.Instance instance, int dataAt, HavokSkeletonMapper read)
    {
        Instance = instance;
        DataAt = dataAt;
        ObjectId = read.ObjectId;
        SkeletonAObjectId = read.SkeletonAObjectId;
        SkeletonBObjectId = read.SkeletonBObjectId;
        SkeletonA = read.SkeletonA;
        SkeletonB = read.SkeletonB;
        MappingType = read.MappingType;
        KeepUnmappedLocal = read.KeepUnmappedLocal;
        UnmappedBones = read.UnmappedBones;
        ExtractedMotionMapping = read.ExtractedMotionMapping;
        Rows = read.Rows.ToList();
        ChainMappings = read.ChainMappings.ToList();
        OptionalMembersRead = read.OptionalMembersRead;
    }

    private SkeletonMapperNode(SkeletonMapperNode other)
    {
        Instance = other.Instance;
        DataAt = other.DataAt;
        ObjectId = other.ObjectId;
        SkeletonAObjectId = other.SkeletonAObjectId;
        SkeletonBObjectId = other.SkeletonBObjectId;
        SkeletonA = other.SkeletonA;
        SkeletonB = other.SkeletonB;
        MappingType = other.MappingType;
        KeepUnmappedLocal = other.KeepUnmappedLocal;
        UnmappedBones = other.UnmappedBones;
        ExtractedMotionMapping = other.ExtractedMotionMapping;
        Rows = other.Rows.ToList();
        ChainMappings = other.ChainMappings.ToList();
        OptionalMembersRead = other.OptionalMembersRead;
    }

    internal PackfileObjects.Instance Instance { get; }

    internal int DataAt { get; }

    internal bool OptionalMembersRead { get; }

    public int ObjectId { get; }

    public int SkeletonAObjectId { get; }

    public int SkeletonBObjectId { get; }

    public HavokRagdollSkeleton SkeletonA { get; }

    public HavokRagdollSkeleton SkeletonB { get; }

    public IReadOnlyList<int> UnmappedBones { get; }

    public HavokSkeletonMappingTransform ExtractedMotionMapping { get; }

    public List<HavokSkeletonSimpleMappingRow> Rows { get; }

    public List<HavokSkeletonChainMappingRow> ChainMappings { get; }

    public int MappingType { get; set; }

    public bool KeepUnmappedLocal { get; set; }

    internal SkeletonMapperNode Copy() => new(this);

    internal bool SameAs(SkeletonMapperNode other) =>
        MappingType == other.MappingType &&
        KeepUnmappedLocal == other.KeepUnmappedLocal &&
        Rows.SequenceEqual(other.Rows) &&
        ChainMappings.SequenceEqual(other.ChainMappings);
}

public sealed class SkeletonMapperDocument
{
    private const string MapperClass = "hkaSkeletonMapper";

    private byte[] _source;
    private DocumentSourceStamp _stamp;

    private readonly List<SkeletonMapperNode> _nodes = new();
    private readonly List<SkeletonMapperNode> _saved = new();

    private SkeletonMapperDocument(byte[] source, string? path)
    {
        ArgumentNullException.ThrowIfNull(source);

        Path = path;
        _source = (byte[])source.Clone();
        _stamp = DocumentSourceStamp.Capture(_source);
        Reload();
    }

    public SkeletonMapperDocument(byte[] source) : this(source, null)
    {
    }

    public static SkeletonMapperDocument Open(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        string full = System.IO.Path.GetFullPath(path);
        return new SkeletonMapperDocument(InputFilePolicy.ReadHkx(full), full);
    }

    public string? Path { get; }

    public bool Dirty => Changed().Count > 0;

    public IReadOnlyList<SkeletonMapperNode> Mappers => _nodes;

    public static HavokSkeletonMappingTransform RecomputedTransform(
        HavokRagdollSkeleton skeletonA, int boneA, HavokRagdollSkeleton skeletonB, int boneB)
    {
        var row = SkeletonMapperAuthor.Recompute(skeletonA, boneA, skeletonB, boneB);
        return HavokSkeletonMappingTransform.From(row.AFromBTranslation, row.AFromBRotation, Vector3.One);
    }

    public void SetSimpleRowBones(int mapper, int row, int boneA, int boneB)
    {
        var node = Node(mapper);
        HavokSkeletonSimpleMappingRow current = Row(node, row);
        CheckBone(node.SkeletonA, boneA, "A");
        CheckBone(node.SkeletonB, boneB, "B");

        var rows = new List<HavokSkeletonSimpleMappingRow>(node.Rows);
        rows[row] = current with { BoneA = boneA, BoneB = boneB };
        RequireNoNewDuplicates(node, rows);
        node.Rows[row] = rows[row];
    }

    public void SetSimpleRowTransform(int mapper, int row, HavokSkeletonMappingTransform transform)
    {
        var node = Node(mapper);
        HavokSkeletonSimpleMappingRow current = Row(node, row);
        CheckTransform(transform, $"row {row} of mapper #{node.ObjectId}");
        node.Rows[row] = current with { AFromB = transform };
    }

    public void SetSimpleRowTransform(int mapper, int row, Vector3 translation, Quaternion rotation)
    {
        var node = Node(mapper);
        HavokSkeletonSimpleMappingRow current = Row(node, row);
        CheckTransform(HavokSkeletonMappingTransform.From(translation, rotation, current.AFromB.Scale),
                       $"row {row} of mapper #{node.ObjectId}");
        node.Rows[row] = current with
        {
            AFromB = current.AFromB.WithTranslation(translation).WithRotation(rotation),
        };
    }

    public int AddSimpleRow(int mapper, int boneA, int boneB, bool allowDuplicatePair = false) =>
        InsertSimpleRow(mapper, Node(mapper).Rows.Count, boneA, boneB, allowDuplicatePair);

    public int InsertSimpleRow(int mapper, int index, int boneA, int boneB, bool allowDuplicatePair = false)
    {
        var node = Node(mapper);
        CheckBone(node.SkeletonA, boneA, "A");
        CheckBone(node.SkeletonB, boneB, "B");

        HavokSkeletonMappingTransform transform;
        try
        {
            transform = RecomputedTransform(node.SkeletonA, boneA, node.SkeletonB, boneB);
        }
        catch (ArgumentException error)
        {
            throw new InvalidOperationException(error.Message, error);
        }

        return InsertSimpleRow(mapper, index,
            new HavokSkeletonSimpleMappingRow(boneA, boneB, transform), allowDuplicatePair);
    }

    public int InsertSimpleRow(int mapper, int index, HavokSkeletonSimpleMappingRow row,
                               bool allowDuplicatePair = false)
    {
        var node = Node(mapper);
        if (index < 0 || index > node.Rows.Count)
            throw new InvalidOperationException(
                $"this mapper has {node.Rows.Count} simple row(s), so there is no position {index} to insert at");
        CheckBone(node.SkeletonA, row.BoneA, "A");
        CheckBone(node.SkeletonB, row.BoneB, "B");
        CheckTransform(row.AFromB, $"the row to insert into mapper #{node.ObjectId}");

        var rows = new List<HavokSkeletonSimpleMappingRow>(node.Rows);
        rows.Insert(index, row);
        if (!allowDuplicatePair) RequireNoNewDuplicates(node, rows);
        node.Rows.Insert(index, row);
        return index;
    }

    public void RemoveSimpleRow(int mapper, int row)
    {
        var node = Node(mapper);
        Row(node, row);
        node.Rows.RemoveAt(row);
    }

    public void SetChainRow(int mapper, int row, int startBoneA, int endBoneA, int startBoneB, int endBoneB)
    {
        var node = Node(mapper);
        HavokSkeletonChainMappingRow current = Chain(node, row);
        CheckBone(node.SkeletonA, startBoneA, "A");
        CheckBone(node.SkeletonA, endBoneA, "A");
        CheckBone(node.SkeletonB, startBoneB, "B");
        CheckBone(node.SkeletonB, endBoneB, "B");

        node.ChainMappings[row] = current with
        {
            StartBoneA = startBoneA,
            EndBoneA = endBoneA,
            StartBoneB = startBoneB,
            EndBoneB = endBoneB,
        };
    }

    public void SetChainRowTransforms(int mapper, int row, HavokSkeletonMappingTransform start,
                                      HavokSkeletonMappingTransform end)
    {
        var node = Node(mapper);
        HavokSkeletonChainMappingRow current = Chain(node, row);
        CheckTransform(start, $"the start transform of chain row {row} of mapper #{node.ObjectId}");
        CheckTransform(end, $"the end transform of chain row {row} of mapper #{node.ObjectId}");
        node.ChainMappings[row] = current with { StartAFromB = start, EndAFromB = end };
    }

    public void SetMappingType(int mapper, int mappingType)
    {
        var node = Node(mapper);
        if (mappingType is not (SkeletonMapperAuthor.RagdollMappingType or 1))
            throw new InvalidOperationException(
                $"mapping type {mappingType} is not one this build writes; the generator's own table " +
                "holds 0 (ragdoll) and 1 (retargeting) and refuses anything else");
        node.MappingType = mappingType;
    }

    public void SetKeepUnmappedLocal(int mapper, bool value) => Node(mapper).KeepUnmappedLocal = value;

    public int RecomputeSimpleRow(int mapper, int row)
    {
        var node = Node(mapper);
        HavokSkeletonSimpleMappingRow current = Row(node, row);
        HavokRagdollMapping recomputed;
        try
        {
            recomputed = SkeletonMapperAuthor.Recompute(node.SkeletonA, current.BoneA, node.SkeletonB, current.BoneB);
        }
        catch (ArgumentException error)
        {
            throw new InvalidOperationException(error.Message, error);
        }

        node.Rows[row] = current with
        {
            AFromB = current.AFromB
                .WithTranslation(recomputed.AFromBTranslation)
                .WithRotation(recomputed.AFromBRotation),
        };
        return 1;
    }

    public int RecomputeSimpleRows(int mapper)
    {
        var node = Node(mapper);
        int recomputed = 0;
        for (int row = 0; row < node.Rows.Count; row++) recomputed += RecomputeSimpleRow(mapper, row);
        return recomputed;
    }

    public int RecomputeAllSimpleRows()
    {
        int recomputed = 0;
        for (int mapper = 0; mapper < _nodes.Count; mapper++) recomputed += RecomputeSimpleRows(mapper);
        return recomputed;
    }

    public byte[] Serialize()
    {
        var changed = Changed();
        if (changed.Count == 0) return (byte[])_source.Clone();
        return SkeletonMapperWriteback.Write(_source, _nodes, changed);
    }

    internal byte[] ForceRebuild() =>
        SkeletonMapperWriteback.Write(_source, _nodes, Array.Empty<SkeletonMapperChange>(), force: true);

    public void Save()
    {
        string path = Path
            ?? throw new InvalidOperationException("this document was not opened from a file, so it has nowhere to save");
        if (!Dirty) return;

        byte[] bytes = Serialize();
        FileSafety.ReplaceChecked(path, bytes, _stamp);
        _source = bytes;
        _stamp = DocumentSourceStamp.Capture(_source);
        Reload();
    }

    private void Reload()
    {
        _nodes.Clear();
        _saved.Clear();

        var image = PackfileImage.Read(_source);
        NativeLayout.RequireWritable(image);

        var objects = new PackfileObjects(image);
        foreach (var instance in objects.Instances)
        {
            if (instance.ClassName != MapperClass) continue;
            if (SkeletonMapperReader.Read(objects, instance) is not { } mapper) continue;
            if (!mapper.OptionalMembersRead)
                throw new InvalidDataException(
                    $"mapper #{mapper.ObjectId} carries chain mappings, unmapped bones, keepUnmappedLocal or " +
                    "an extracted motion mapping this build cannot read, so nothing was loaded: a write-back " +
                    "would silently drop them");
            if (objects.FieldAt(instance, "mapping") is not int dataAt) continue;
            _nodes.Add(new SkeletonMapperNode(instance, dataAt, mapper));
        }

        if (_nodes.Count == 0)
            throw new InvalidDataException(
                "This file holds no hkaSkeletonMapper this build can read, so there is nothing to edit.");

        foreach (var node in _nodes) _saved.Add(node.Copy());
    }

    private List<SkeletonMapperChange> Changed()
    {
        var changed = new List<SkeletonMapperChange>();
        for (int i = 0; i < _nodes.Count; i++)
            if (!_nodes[i].SameAs(_saved[i]))
                changed.Add(new SkeletonMapperChange(_nodes[i], _saved[i]));
        return changed;
    }

    private SkeletonMapperNode Node(int mapper)
    {
        if (mapper < 0 || mapper >= _nodes.Count)
            throw new InvalidOperationException(
                $"this file holds {_nodes.Count} mapper(s), so there is no mapper {mapper}");
        return _nodes[mapper];
    }

    private HavokSkeletonSimpleMappingRow Row(SkeletonMapperNode node, int row)
    {
        if (row < 0 || row >= node.Rows.Count)
            throw new InvalidOperationException(
                $"mapper #{node.ObjectId} holds {node.Rows.Count} simple row(s), so there is no row {row}");
        return node.Rows[row];
    }

    private HavokSkeletonChainMappingRow Chain(SkeletonMapperNode node, int row)
    {
        if (row < 0 || row >= node.ChainMappings.Count)
            throw new InvalidOperationException(
                $"mapper #{node.ObjectId} holds {node.ChainMappings.Count} chain row(s), so there is no chain row {row}");
        return node.ChainMappings[row];
    }

    private static void CheckBone(HavokRagdollSkeleton skeleton, int bone, string side)
    {
        if (bone < 0 || bone >= skeleton.BoneNames.Count)
            throw new InvalidOperationException(
                $"bone {bone} is not a bone of the {side} skeleton '{skeleton.Name}', which holds " +
                $"{skeleton.BoneNames.Count} bone(s)");
    }

    private static void CheckTransform(HavokSkeletonMappingTransform transform, string what)
    {
        if (!transform.IsFinite)
            throw new InvalidOperationException($"{what} holds a non-finite lane, so it was not written");
        if (!transform.HasUnitRotation(SkeletonMapperAuthor.UnitTolerance))
            throw new InvalidOperationException(
                $"{what} holds a rotation of length {transform.Rotation.Length():0.######}, which is not a unit quaternion");
    }

    private void RequireNoNewDuplicates(SkeletonMapperNode node,
                                        IReadOnlyList<HavokSkeletonSimpleMappingRow> rows)
    {
        int at = _nodes.IndexOf(node);
        if (at < 0) throw new InvalidOperationException("this mapper is not part of this document");

        var (wasA, wasB) = Duplicates(_saved[at].Rows);
        var (nowA, nowB) = Duplicates(rows);
        if (nowA > wasA || nowB > wasB)
            throw new InvalidOperationException(
                "that bone is already mapped in this mapper; a second row would pair one bone twice, " +
                "which the generator that produced the shipped mappings refuses");
    }

    private static (int SideA, int SideB) Duplicates(IReadOnlyList<HavokSkeletonSimpleMappingRow> rows)
    {
        int a = rows.GroupBy(row => row.BoneA).Count(group => group.Count() > 1);
        int b = rows.GroupBy(row => row.BoneB).Count(group => group.Count() > 1);
        return (a, b);
    }
}
