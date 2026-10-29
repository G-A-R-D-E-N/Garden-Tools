using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Text;
using System.Text.Json;

namespace OpenCommonwealth.Services.Hkx;

public static class SkeletonJson
{
    public const string Format = "fo4-skeleton";
    public const int Version = 1;

    public const string UnitsNote =
        "raw Havok units and game axes, unconverted; the importer owns any unit or axis change";

    public static string Write(HkxSkeleton skeleton, string sourcePath)
    {
        var buffer = new MemoryStream();
        using (var w = new Utf8JsonWriter(buffer, new JsonWriterOptions { Indented = true }))
        {
            w.WriteStartObject();
            w.WriteString("format", Format);
            w.WriteNumber("version", Version);
            w.WriteString("units", UnitsNote);
            w.WriteString("source", Path.GetFileName(sourcePath));
            w.WriteString("name", skeleton.Name);
            w.WriteNumber("boneCount", skeleton.BoneNames.Count);

            w.WriteStartArray("bones");
            for (int i = 0; i < skeleton.BoneNames.Count; i++)
            {
                var pose = i < skeleton.ReferencePose.Count ? skeleton.ReferencePose[i] : new HkxBonePose();
                string bone = $"bones[{i}]";

                w.WriteStartObject();
                w.WriteString("name", skeleton.BoneNames[i]);
                w.WriteNumber("parent", i < skeleton.ParentIndices.Count ? skeleton.ParentIndices[i] : -1);
                WriteVector(w, "translation", pose.Translation, bone);
                WriteQuaternion(w, "rotation", pose.Rotation, bone);
                WriteVector(w, "scale", pose.Scale, bone);
                if (i < skeleton.LockTranslation.Count && skeleton.LockTranslation[i])
                    w.WriteBoolean("lockTranslation", true);
                w.WriteEndObject();
            }
            w.WriteEndArray();

            if (skeleton.FloatSlots.Count > 0)
            {
                w.WriteStartArray("floatSlots");
                foreach (string? slot in skeleton.FloatSlots) w.WriteStringValue(slot);
                w.WriteEndArray();
            }

            if (skeleton.ReferenceFloats.Count > 0)
            {
                w.WriteStartArray("referenceFloats");
                for (int i = 0; i < skeleton.ReferenceFloats.Count; i++)
                    WriteNumber(w, $"referenceFloats[{i}]", skeleton.ReferenceFloats[i]);
                w.WriteEndArray();
            }

            if (skeleton.Partitions.Count > 0)
            {
                w.WriteStartArray("partitions");
                foreach (var partition in skeleton.Partitions)
                {
                    w.WriteStartObject();
                    w.WriteString("name", partition.Name);
                    w.WriteNumber("startBoneIndex", partition.StartBoneIndex);
                    w.WriteNumber("numBones", partition.NumBones);
                    w.WriteEndObject();
                }
                w.WriteEndArray();
            }

            if (skeleton.LocalFrames.Count > 0)
            {
                w.WriteStartArray("localFrames");
                foreach (var frame in skeleton.LocalFrames)
                {
                    w.WriteStartObject();
                    w.WriteNumber("boneIndex", frame.BoneIndex);
                    w.WriteBoolean("hasFrame", frame.HasFrame);
                    if (frame.FrameOffset >= 0) w.WriteNumber("frameOffset", frame.FrameOffset);
                    w.WriteEndObject();
                }
                w.WriteEndArray();
            }

            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(buffer.ToArray());
    }

    public static HkxSkeleton Read(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException($"a {Format} document is a JSON object, not {root.ValueKind}");

        string format = ReadString(root, "format", "format");
        if (format != Format) throw new InvalidOperationException($"not a {Format} document: '{format}'");

        var skeleton = new HkxSkeleton { Name = ReadString(root, "name", "name") };

        JsonElement bones = Required(root, "bones", "bones");
        if (bones.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("bones must be an array");

        int index = 0;
        foreach (var bone in bones.EnumerateArray())
        {
            string member = $"bones[{index}]";
            if (bone.ValueKind != JsonValueKind.Object)
                throw new InvalidDataException($"{member} must be an object");

            skeleton.BoneNames.Add(ReadString(bone, "name", $"{member}.name"));
            skeleton.ParentIndices.Add(ReadInt32(bone, "parent", $"{member}.parent"));
            skeleton.ReferencePose.Add(new HkxBonePose(
                ReadVector(bone, "translation", member),
                ReadQuaternion(bone, "rotation", member),
                ReadVector(bone, "scale", member)));
            skeleton.LockTranslation.Add(ReadBoolean(bone, "lockTranslation", $"{member}.lockTranslation"));
            index++;
        }

        if (OptionalArray(root, "floatSlots", out var slots))
        {
            int at = 0;
            foreach (var value in slots.EnumerateArray())
                skeleton.FloatSlots.Add(ReadNullableString(value, $"floatSlots[{at++}]"));
        }

        if (OptionalArray(root, "referenceFloats", out var floats))
        {
            int at = 0;
            foreach (var value in floats.EnumerateArray())
            {
                if (value.ValueKind != JsonValueKind.Number || !value.TryGetSingle(out float number))
                    throw new InvalidDataException($"referenceFloats[{at}] is not a number");
                skeleton.ReferenceFloats.Add(number);
                at++;
            }
        }

        if (OptionalArray(root, "partitions", out var partitions))
        {
            int at = 0;
            foreach (var partition in partitions.EnumerateArray())
            {
                string member = $"partitions[{at}]";
                if (partition.ValueKind != JsonValueKind.Object)
                    throw new InvalidDataException($"{member} must be an object");

                skeleton.Partitions.Add(new HkxSkeletonPartition
                {
                    Name = OptionalString(partition, "name", $"{member}.name"),
                    StartBoneIndex = OptionalInt32(partition, "startBoneIndex", $"{member}.startBoneIndex", -1),
                    NumBones = OptionalInt32(partition, "numBones", $"{member}.numBones", 0),
                });
                at++;
            }
        }

        if (OptionalArray(root, "localFrames", out var frames))
        {
            int at = 0;
            foreach (var frame in frames.EnumerateArray())
            {
                string member = $"localFrames[{at}]";
                if (frame.ValueKind != JsonValueKind.Object)
                    throw new InvalidDataException($"{member} must be an object");

                skeleton.LocalFrames.Add(new HkxSkeletonLocalFrame
                {
                    BoneIndex = OptionalInt32(frame, "boneIndex", $"{member}.boneIndex", -1),
                    HasFrame = OptionalBoolean(frame, "hasFrame", $"{member}.hasFrame", false),
                    FrameOffset = OptionalInt32(frame, "frameOffset", $"{member}.frameOffset", -1),
                });
                at++;
            }
        }

        return skeleton;
    }

    private static void WriteNumber(Utf8JsonWriter w, string member, float value)
    {
        if (!float.IsFinite(value))
            throw new InvalidDataException(
                $"{member} is {value}, which a {Format} document cannot carry; the value is refused rather than changed");
        w.WriteNumberValue(value);
    }

    private static void WriteVector(Utf8JsonWriter w, string name, Vector3 v, string member)
    {
        w.WriteStartArray(name);
        WriteNumber(w, $"{member}.{name}[0]", v.X);
        WriteNumber(w, $"{member}.{name}[1]", v.Y);
        WriteNumber(w, $"{member}.{name}[2]", v.Z);
        w.WriteEndArray();
    }

    private static void WriteQuaternion(Utf8JsonWriter w, string name, Quaternion q, string member)
    {
        w.WriteStartArray(name);
        WriteNumber(w, $"{member}.{name}[0]", q.X);
        WriteNumber(w, $"{member}.{name}[1]", q.Y);
        WriteNumber(w, $"{member}.{name}[2]", q.Z);
        WriteNumber(w, $"{member}.{name}[3]", q.W);
        w.WriteEndArray();
    }

    private static JsonElement Required(JsonElement parent, string property, string member)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(property, out var value))
            throw new InvalidDataException($"{member} is missing");
        return value;
    }

    private static string ReadString(JsonElement parent, string property, string member)
    {
        var value = Required(parent, property, member);
        if (value.ValueKind != JsonValueKind.String || value.GetString() is not string text)
            throw new InvalidDataException($"{member} is not a string");
        return text;
    }

    private static int ReadInt32(JsonElement parent, string property, string member)
    {
        var value = Required(parent, property, member);
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int number))
            throw new InvalidDataException($"{member} is not a 32-bit integer");
        return number;
    }

    private static bool ReadBoolean(JsonElement parent, string property, string member)
    {
        if (!parent.TryGetProperty(property, out var value) || value.ValueKind == JsonValueKind.Null) return false;
        if (value.ValueKind == JsonValueKind.True) return true;
        if (value.ValueKind == JsonValueKind.False) return false;
        throw new InvalidDataException($"{member} is not a boolean");
    }

    private static bool OptionalArray(JsonElement root, string property, out JsonElement value)
    {
        if (!root.TryGetProperty(property, out value) || value.ValueKind == JsonValueKind.Null) return false;
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException($"{property} must be an array");
        return true;
    }

    private static string? ReadNullableString(JsonElement value, string member)
    {
        if (value.ValueKind == JsonValueKind.Null) return null;
        if (value.ValueKind != JsonValueKind.String || value.GetString() is not string text)
            throw new InvalidDataException($"{member} is not a string");
        return text;
    }

    private static string? OptionalString(JsonElement parent, string property, string member)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(property, out var value))
            return null;
        return ReadNullableString(value, member);
    }

    private static int OptionalInt32(JsonElement parent, string property, string member, int fallback)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(property, out var value) ||
            value.ValueKind == JsonValueKind.Null)
            return fallback;
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int number))
            throw new InvalidDataException($"{member} is not a 32-bit integer");
        return number;
    }

    private static bool OptionalBoolean(JsonElement parent, string property, string member, bool fallback)
    {
        if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(property, out var value) ||
            value.ValueKind == JsonValueKind.Null)
            return fallback;
        if (value.ValueKind == JsonValueKind.True) return true;
        if (value.ValueKind == JsonValueKind.False) return false;
        throw new InvalidDataException($"{member} is not a boolean");
    }

    private static Vector3 ReadVector(JsonElement parent, string property, string member)
    {
        string path = $"{member}.{property}";
        var array = Required(parent, property, path);
        if (array.ValueKind != JsonValueKind.Array || array.GetArrayLength() != 3)
            throw new InvalidDataException($"{path} must be an array of three numbers");
        return new Vector3(Number(array, 0, path), Number(array, 1, path), Number(array, 2, path));
    }

    private static Quaternion ReadQuaternion(JsonElement parent, string property, string member)
    {
        string path = $"{member}.{property}";
        var array = Required(parent, property, path);
        if (array.ValueKind != JsonValueKind.Array || array.GetArrayLength() != 4)
            throw new InvalidDataException($"{path} must be an array of four numbers");
        return new Quaternion(Number(array, 0, path), Number(array, 1, path),
                              Number(array, 2, path), Number(array, 3, path));
    }

    private static float Number(JsonElement array, int at, string member)
    {
        var value = array[at];
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetSingle(out float number))
            throw new InvalidDataException($"{member}[{at}] is not a number");
        return number;
    }

    public static List<int> ChildCounts(HkxSkeleton skeleton)
    {
        var counts = new List<int>(new int[skeleton.BoneNames.Count]);
        foreach (int parent in skeleton.ParentIndices)
            if (parent >= 0 && parent < counts.Count) counts[parent]++;
        return counts;
    }
}
