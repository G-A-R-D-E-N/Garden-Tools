using System.Collections.Generic;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools.Export;

public static class SkeletonExport
{
    public static string Json(HkxSkeleton skeleton, string source)
    {
        var notes = new List<string>();
        if (skeleton.ParentIndices.Count != skeleton.BoneNames.Count)
            notes.Add($"parent indices {skeleton.ParentIndices.Count} for {skeleton.BoneNames.Count} bones");
        if (skeleton.ReferencePose.Count != skeleton.BoneNames.Count)
            notes.Add($"reference pose {skeleton.ReferencePose.Count} for {skeleton.BoneNames.Count} bones");

        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.Skeleton)
            .Name("version").Number(ExportFormats.Version)
            .Name("source").Text(source)
            .Name("name").Text(skeleton.Name)
            .Name("boneCount").Number(skeleton.BoneNames.Count)
            .Name("bones").Array();

        for (int i = 0; i < skeleton.BoneNames.Count; i++)
        {
            json.Object()
                .Name("index").Number(i)
                .Name("name").Text(skeleton.BoneNames[i]);

            if (i < skeleton.ParentIndices.Count) json.Name("parent").Number(skeleton.ParentIndices[i]);
            else json.Name("parent").Null();

            json.EndObject();
        }

        json.EndArray().Name("referencePose").Array();

        foreach (var pose in skeleton.ReferencePose)
        {
            json.Object()
                .Name("translation").Vector(pose.Translation)
                .Name("rotation").Quaternion(pose.Rotation)
                .Name("scale").Vector(pose.Scale)
                .EndObject();
        }

        json.EndArray().Name("notes").Array();
        foreach (string note in notes) json.Text(note);
        json.EndArray();

        return json.EndObject().Finish();
    }
}
