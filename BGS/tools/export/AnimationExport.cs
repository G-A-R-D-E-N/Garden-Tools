using System;
using System.Collections.Generic;
using System.IO;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools.Export;

public static class AnimationExport
{
    public static string Json(HkxAnimationData animation, string source)
    {
        if (Array.IndexOf(HkxAnimationData.ExportableAnimationClasses, animation.AnimationClass) < 0)
            throw new InvalidDataException($"this file holds {Describe(animation)}, which is not exported yet");

        if (animation.NumFrames <= 0 || animation.NumTracks <= 0)
            throw new InvalidDataException(
                $"this animation decoded to {animation.NumFrames} frame(s) of {animation.NumTracks} track(s)");

        if (animation.Tracks.Count != animation.NumTracks)
            throw new InvalidDataException(
                $"this animation declares {animation.NumTracks} track(s) and decoded to {animation.Tracks.Count}");

        var notes = new List<string>();
        if (animation.TrackToBoneIndices.Count != animation.NumTracks)
            notes.Add($"track-to-bone mapping {animation.TrackToBoneIndices.Count} for {animation.NumTracks} tracks");

        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.Animation)
            .Name("version").Number(ExportFormats.Version)
            .Name("source").Text(source)
            .Name("class").Text(animation.AnimationClass)
            .Name("duration").Number(animation.Duration)
            .Name("numFrames").Number(animation.NumFrames)
            .Name("numTracks").Number(animation.NumTracks)
            .Name("frameDuration").Number(animation.FrameDuration)
            .Name("skeletonName").Text(animation.OriginalSkeletonName)
            .Name("blendHint").Number(animation.BlendHint)
            .Name("blocks").Object()
                .Name("numBlocks").Number(animation.NumBlocks)
                .Name("maxFramesPerBlock").Number(animation.MaxFramesPerBlock)
                .Name("blockDuration").Number(animation.BlockDuration)
            .EndObject()
            .Name("boneNames").Array();

        foreach (string bone in animation.BoneNames) json.Text(bone);
        json.EndArray().Name("trackToBone").Array();

        foreach (int bone in animation.TrackToBoneIndices) json.Number(bone);
        json.EndArray().Name("events").Array();

        foreach (var annotation in animation.Annotations)
            json.Object().Name("time").Number(annotation.Time).Name("text").Text(annotation.Text).EndObject();

        json.EndArray().Name("tracks").Array();

        for (int t = 0; t < animation.Tracks.Count; t++)
        {
            var track = animation.Tracks[t];
            if (track.Translations.Count != animation.NumFrames ||
                track.Rotations.Count != animation.NumFrames ||
                track.Scales.Count != animation.NumFrames)
                throw new InvalidDataException(
                    $"track {t} decoded to a different number of frames than the {animation.NumFrames} declared");

            json.Object().Name("index").Number(t).Name("bone");
            if (t < animation.TrackToBoneIndices.Count) json.Number(animation.TrackToBoneIndices[t]);
            else json.Null();

            json.Name("frames").Array();
            for (int f = 0; f < animation.NumFrames; f++)
            {
                json.Object()
                    .Name("translation").Vector(track.Translations[f])
                    .Name("rotation").Quaternion(track.Rotations[f])
                    .Name("scale").Vector(track.Scales[f])
                    .EndObject();
            }

            json.EndArray().EndObject();
        }

        json.EndArray().Name("notes").Array();
        foreach (string note in notes) json.Text(note);
        json.EndArray();

        return json.EndObject().Finish();
    }

    private static string Describe(HkxAnimationData animation) =>
        animation.AnimationClass.Length > 0 ? animation.AnimationClass : "no animation class";
}
