using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace OpenCommonwealth.Services.Hkx;

public static class GltfAnimationImport
{
    public const float MetresPerUnit = 0.0142875f;
    public const float MinimumFrameRate = 30f;
    public const int MaximumFrames = 100_000;
    public const long MaximumFrameTracks = 1L << 22;
    private const float RestPositionTolerance = 0.01f;
    private const float RestAngleTolerance = 0.001f;

    private static readonly Matrix4x4 ToHavok = Matrix4x4.CreateRotationX(MathF.PI / 2);

    public sealed record Report(float FrameRate, int Frames, float Duration,
                                IReadOnlyList<string> Animated, IReadOnlyList<string> ReferenceOnly,
                                IReadOnlyList<string> Unmapped, int WeightChannels, int NonBoneChannels,
                                IReadOnlyList<string> RestMismatch);

    public sealed record Result(HkxAnimationData Animation, Report Report, RootMotion.Motion Motion);

    public sealed record Written(byte[] Bytes, float PositionError, float RotationError, float ScaleError,
                                 int AnnotationsCleared, int AnnotationsWritten, int Tracks, bool MotionReplaced,
                                 RootMotion.Motion Motion, int RotationFormat, float RotationTolerance);

    public static Result Build(GltfScene scene, HkxSkeleton skeleton, string? animationName = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(skeleton);

        var bones = BoneIndices(skeleton);

        if (scene.Skins.Count != 1)
            throw Refuse(scene.Skins.Count == 0
                ? "the glTF has no skin, so it names no bones. Export the armature with its skin."
                : $"the glTF holds {scene.Skins.Count} skins. Export one armature at a time.");

        var joints = scene.Skins[0];
        var jointSet = new HashSet<int>(joints);
        var jointNames = new HashSet<string>(StringComparer.Ordinal);
        foreach (int joint in joints)
        {
            string name = scene.Nodes[joint].Name;
            if (name.Length == 0)
                throw Refuse($"joint node {joint} has no name, so it cannot be matched to a skeleton bone.");
            if (!jointNames.Add(name))
                throw Refuse($"the glTF has two joints named '{name}', so a match would be a guess.");
        }

        var clip = ChooseAnimation(scene, animationName);
        var byJoint = new Dictionary<int, Dictionary<string, GltfChannel>>();
        int weights = 0, nonBone = 0;
        foreach (var channel in clip.Channels)
        {
            if (channel.Path == "weights")
            {
                weights++;
                continue;
            }

            if (!jointSet.Contains(channel.Node))
            {
                if (joints.Any(joint => Ancestors(scene, joint).Contains(channel.Node)))
                    throw Refuse($"node '{scene.Nodes[channel.Node].Name}' is animated, is not a bone, and has bones " +
                                 "below it. Only bones can carry animation here, so bake its motion into the bones.");
                nonBone++;
                continue;
            }

            string name = scene.Nodes[channel.Node].Name;
            if (channel.Interpolation is not ("LINEAR" or "STEP" or "CUBICSPLINE"))
                throw Refuse($"bone '{name}' uses {channel.Interpolation} keys for its {channel.Path}. Only LINEAR, " +
                             "STEP and CUBICSPLINE keys are read, so re-export with one of those.");

            if (!byJoint.TryGetValue(channel.Node, out var perPath))
                byJoint[channel.Node] = perPath = new Dictionary<string, GltfChannel>(StringComparer.Ordinal);
            if (!perPath.TryAdd(channel.Path, channel))
                throw Refuse($"bone '{name}' has two {channel.Path} channels.");
        }

        if (byJoint.Count == 0)
            throw Refuse("no channel animates a bone of the skin, so there is nothing to write.");

        var missing = byJoint.Keys.Select(node => scene.Nodes[node].Name).Where(name => !bones.ContainsKey(name)).ToList();
        if (missing.Count > 0)
            throw Refuse("the glTF animates bone(s) the skeleton does not have: " + string.Join(", ", missing) +
                         ". Refused rather than guessing a pairing; rename them to the skeleton's names or remove them.");

        var boneNode = new Dictionary<int, int>();
        var restMismatch = new List<string>();
        foreach (int node in byJoint.Keys)
        {
            string name = scene.Nodes[node].Name;
            int bone = bones[name];
            boneNode[bone] = node;

            int jointParent = JointParent(scene, jointSet, node);
            string? gltfParent = jointParent >= 0 ? scene.Nodes[jointParent].Name : null;
            int skeletonParent = skeleton.ParentIndices.Count > bone ? skeleton.ParentIndices[bone] : -1;
            string? rigParent = skeletonParent >= 0 && skeletonParent < skeleton.BoneNames.Count
                ? skeleton.BoneNames[skeletonParent] : null;
            if (gltfParent != rigParent)
                throw Refuse($"bone '{name}' sits under {gltfParent ?? "nothing"} in the glTF but under " +
                             $"{rigParent ?? "nothing"} in the skeleton. Refused rather than guessing a pairing.");

            var local = scene.Nodes[node];
            var reference = Reference(skeleton, bone);
            if (!HavokPose(scene, jointSet, node, local.Translation, local.Rotation, local.Scale,
                           out var restTranslation, out var restRotation, out _) ||
                (restTranslation - reference.Translation).Length() > RestPositionTolerance ||
                Angle(restRotation, reference.Rotation) > RestAngleTolerance)
                restMismatch.Add(name);
        }

        float step = float.MaxValue, end = 0;
        foreach (var channels in byJoint.Values)
            foreach (var channel in channels.Values)
            {
                for (int k = 1; k < channel.Times.Length; k++)
                    step = MathF.Min(step, channel.Times[k] - channel.Times[k - 1]);
                if (channel.Times.Length > 0) end = MathF.Max(end, channel.Times[^1]);
            }

        float frameRate = step < float.MaxValue
            ? MathF.Max(MinimumFrameRate, MathF.Ceiling(1f / step - 1e-3f))
            : MinimumFrameRate;
        float span = end * frameRate;
        if (!(span < MaximumFrames))
            throw Refuse($"the animation runs {end:F3} s, more than the supported {MaximumFrames} frames at {frameRate} per second.");
        int frames = (int)MathF.Ceiling(span - 1e-3f) + 1;
        if (frames < 2)
            throw Refuse("the animated keys span less than two frames, so there is no interval to store.");
        if ((long)frames * skeleton.BoneNames.Count > MaximumFrameTracks)
            throw Refuse($"the clip would be {frames} frame(s) over {skeleton.BoneNames.Count} track(s), which is " +
                         $"{(long)frames * skeleton.BoneNames.Count:N0} frame-tracks, more than the {MaximumFrameTracks:N0} " +
                         "the read-back allows. Refused before baking.");

        float duration = (frames - 1) / frameRate;
        var notes = Notes(clip, frameRate, duration);
        int root = RootBone(skeleton);

        var tracks = new List<HkxTrackData>(skeleton.BoneNames.Count);
        var animated = new List<string>();
        var referenceOnly = new List<string>();
        for (int bone = 0; bone < skeleton.BoneNames.Count; bone++)
        {
            var reference = Reference(skeleton, bone);
            var track = new HkxTrackData { RotationAnimated = true };
            for (int axis = 0; axis < 3; axis++)
            {
                track.TranslationAnimated[axis] = true;
                track.ScaleAnimated[axis] = true;
            }

            if (boneNode.TryGetValue(bone, out int node))
            {
                animated.Add(skeleton.BoneNames[bone]);
                Bake(scene, jointSet, node, byJoint[node], reference, frameRate, frames, track);
            }
            else
            {
                referenceOnly.Add(skeleton.BoneNames[bone]);
                for (int f = 0; f < frames; f++)
                {
                    track.Translations.Add(reference.Translation);
                    track.Rotations.Add(reference.Rotation);
                    track.Scales.Add(reference.Scale);
                }
            }

            tracks.Add(track);
        }

        var motion = boneNode.ContainsKey(root)
            ? SplitRoot(tracks[root], skeleton.BoneNames[root], duration)
            : Stationary(frames, duration);

        var unmapped = joints.Select(joint => scene.Nodes[joint].Name).Where(name => !bones.ContainsKey(name)).ToList();
        var animation = new HkxAnimationData
        {
            Duration = duration,
            NumFrames = frames,
            NumTracks = tracks.Count,
            FrameDuration = 1f / frameRate,
            BoneNames = skeleton.BoneNames.ToList(),
            TrackToBoneIndices = Enumerable.Range(0, tracks.Count).ToList(),
            OriginalSkeletonName = skeleton.Name,
            Skeleton = skeleton,
            Tracks = tracks,
            Annotations = notes,
        };

        var report = new Report(frameRate, frames, animation.Duration, animated, referenceOnly, unmapped,
                                weights, nonBone, restMismatch);
        return new Result(animation, report, motion);
    }

    public static Written Write(byte[] template, Result imported, HkxSkeleton skeleton)
    {
        ArgumentNullException.ThrowIfNull(template);
        ArgumentNullException.ThrowIfNull(imported);
        ArgumentNullException.ThrowIfNull(skeleton);

        var source = new HkxBinaryReader().ReadAnimation(template);
        int[] map = TemplateBones(source, skeleton.BoneNames.Count);
        var trackOf = new HashSet<int>(map);
        var bones = BoneIndices(skeleton);
        var untracked = imported.Report.Animated.Where(name => !trackOf.Contains(bones[name])).ToList();
        if (untracked.Count > 0)
            throw Refuse("the glTF animates bone(s) the template has no track for: " + string.Join(", ", untracked) +
                         ". Refused rather than writing them onto no track.");

        var animation = Project(imported.Animation, map);
        var motion = RootMotion.Read(template);
        bool hadMotion = motion.UnsupportedReason != null || motion.Samples.Count > 0;

        var budget = new SplineEncoder.Options();
        (byte[] Bytes, (float Position, float Rotation, float Scale) Error) attempt = default;
        SplineEncoder.Options used = budget;
        foreach (var options in Attempts)
        {
            attempt = Encode(template, source, animation, imported.Motion, options);
            used = options;
            if (Within(attempt.Error, budget)) break;
        }
        if (!Within(attempt.Error, budget))
            throw Refuse($"the written curves read back {attempt.Error.Position:F4} unit(s), {attempt.Error.Rotation:F5} " +
                         $"radian(s) and {attempt.Error.Scale:F5} of scale from the sampled pose, over the encoder's " +
                         "budget at every setting tried, so nothing was written.");

        return new Written(attempt.Bytes, attempt.Error.Position, attempt.Error.Rotation, attempt.Error.Scale,
                           source.Annotations.Count, animation.Annotations.Count, animation.NumTracks, hadMotion,
                           imported.Motion, used.RotationFormat, used.RotationTolerance);
    }

    private static readonly SplineEncoder.Options[] Attempts =
    {
        new(),
        new() { RotationTolerance = 0.00025f },
        new() { RotationTolerance = 0.000125f },
        new() { RotationFormat = 2, RotationTolerance = 0.00025f },
        new() { RotationFormat = 2, RotationTolerance = 0.0000625f },
    };

    private static (byte[] Bytes, (float Position, float Rotation, float Scale) Error) Encode(
        byte[] template, HkxAnimationData source, HkxAnimationData animation, RootMotion.Motion root,
        SplineEncoder.Options options)
    {
        var timeline = NativeAnimation.Timeline.Identity(animation) with { Motion = root };
        var native = NativeAnimation.Recompress(template, animation, options, true, timeline, keepAnnotations: false);

        var decoded = new HkxBinaryReader().ReadAnimation(native.Bytes);
        if (source.TrackToBoneIndices.Count > 0 && !decoded.TrackToBoneIndices.SequenceEqual(source.TrackToBoneIndices))
            throw Refuse("the written file does not keep the template's track-to-bone map, so nothing was written.");
        if (!SameNotes(animation.Annotations, decoded.Annotations))
            throw Refuse("the written annotations do not read back as the glTF gave them, so nothing was written.");

        var motion = RootMotion.Read(native.Bytes);
        if (motion.UnsupportedReason != null)
            throw Refuse($"the written root motion is {motion.UnsupportedReason}, which this import does not write, so nothing was written.");
        bool moves = root.Samples.Any(sample => sample.Position != Vector3.Zero || sample.TurnRadians != 0f);
        if (motion.Samples.Count == 0 && moves)
            throw Refuse("the template has no root-motion frame to carry the glTF's root travel, so nothing was written.");
        if (motion.Samples.Count > 0 && !motion.Samples.SequenceEqual(root.Samples))
            throw Refuse("the written root motion does not read back as the glTF gave it, so nothing was written.");

        return (native.Bytes, Measure(animation, decoded));
    }

    private static bool SameNotes(IReadOnlyList<HkxAnnotation> want, IReadOnlyList<HkxAnnotation> got) =>
        want.Count == got.Count &&
        want.Zip(got).All(pair => pair.First.Time == pair.Second.Time && pair.First.Text == pair.Second.Text);

    private static bool Within((float Position, float Rotation, float Scale) error, SplineEncoder.Options budget) =>
        error.Position <= budget.PositionTolerance && error.Rotation <= budget.RotationTolerance &&
        error.Scale <= budget.ScaleTolerance;

    private static int[] TemplateBones(HkxAnimationData source, int bones)
    {
        if (source.Tracks.Count == 0) throw Refuse("the template animation has no tracks.");
        if (source.TrackToBoneIndices.Count == 0 && source.Tracks.Count != bones)
            throw Refuse($"the template animation drives {source.Tracks.Count} track(s) and names no bone for them, while " +
                         $"the skeleton has {bones} bone(s), so which bone each track drives cannot be read.");

        int[] map = source.TrackToBoneIndices.Count > 0
            ? source.TrackToBoneIndices.ToArray()
            : Enumerable.Range(0, bones).ToArray();
        if (map.Length != source.Tracks.Count)
            throw Refuse("the template's track-to-bone map does not match its track count.");
        if (map.Any(bone => bone < 0 || bone >= bones))
            throw Refuse("the template drives a bone index that the skeleton does not have.");
        if (map.Distinct().Count() != map.Length)
            throw Refuse("the template drives one bone with two tracks.");
        return map;
    }

    private static HkxAnimationData Project(HkxAnimationData animation, int[] map) => new()
    {
        Duration = animation.Duration,
        NumFrames = animation.NumFrames,
        NumTracks = map.Length,
        FrameDuration = animation.FrameDuration,
        BoneNames = map.Select(bone => animation.BoneNames[bone]).ToList(),
        TrackToBoneIndices = map.ToList(),
        OriginalSkeletonName = animation.OriginalSkeletonName,
        Skeleton = animation.Skeleton,
        Tracks = map.Select(bone => animation.Tracks[bone]).ToList(),
        Annotations = animation.Annotations,
    };

    private static Dictionary<string, int> BoneIndices(HkxSkeleton skeleton)
    {
        if (skeleton.BoneNames.Count == 0) throw Refuse("the skeleton has no bones.");

        var bones = new Dictionary<string, int>(StringComparer.Ordinal);
        for (int bone = 0; bone < skeleton.BoneNames.Count; bone++)
            if (!bones.TryAdd(skeleton.BoneNames[bone], bone))
                throw Refuse($"the skeleton names two bones '{skeleton.BoneNames[bone]}', so a glTF bone cannot be matched to one.");
        return bones;
    }

    private static GltfAnimation ChooseAnimation(GltfScene scene, string? name)
    {
        if (scene.Animations.Count == 0) throw Refuse("the glTF holds no animation.");

        string names = string.Join(", ", scene.Animations.Select(clip => clip.Name.Length == 0 ? "(unnamed)" : clip.Name));
        if (name == null)
        {
            if (scene.Animations.Count == 1) return scene.Animations[0];
            throw Refuse($"the glTF holds {scene.Animations.Count} animations ({names}). Name the one to import, " +
                         "or export one at a time.");
        }

        var matches = scene.Animations.Where(clip => clip.Name == name).ToList();
        if (matches.Count == 1) return matches[0];
        if (matches.Count > 1)
            throw Refuse($"the glTF holds {matches.Count} animations named '{name}', so the choice would be a guess.");
        throw Refuse($"the glTF has no animation named '{name}'. It holds: {names}.");
    }

    private static List<HkxAnnotation> Notes(GltfAnimation clip, float frameRate, float duration)
    {
        float slack = 0.5f / frameRate;
        var notes = new List<HkxAnnotation>();
        foreach (var note in clip.Annotations)
        {
            if (note.Text.Length == 0)
                throw Refuse($"an annotation at {note.Time:F3} s has no text, so there is no event to write.");
            if (!(note.Time >= 0 && note.Time <= duration + slack))
                throw Refuse($"annotation '{note.Text}' at {note.Time:F3} s is outside the clip, which runs 0 to {duration:F3} s.");
            notes.Add(new HkxAnnotation { Time = MathF.Min(note.Time, duration), Text = note.Text });
        }
        return notes.OrderBy(note => note.Time).ToList();
    }

    private static int RootBone(HkxSkeleton skeleton)
    {
        var roots = Enumerable.Range(0, skeleton.BoneNames.Count)
            .Where(bone => bone >= skeleton.ParentIndices.Count || skeleton.ParentIndices[bone] < 0)
            .ToList();
        if (roots.Count != 1)
            throw Refuse($"the skeleton has {roots.Count} root bone(s), so the glTF's root motion has no single root to come from.");
        return roots[0];
    }

    private static RootMotion.Motion Stationary(int frames, float duration)
    {
        var motion = new RootMotion.Motion { Duration = duration };
        for (int f = 0; f < frames; f++) motion.Samples.Add(new RootMotion.Sample(Vector3.Zero, 0f));
        return motion;
    }

    private static RootMotion.Motion SplitRoot(HkxTrackData track, string name, float duration)
    {
        int frames = track.Rotations.Count;
        var planar = new Vector3[frames];
        var height = new float[frames];
        var turns = new float[frames];
        var swing = new Quaternion[frames];
        float turn = 0;
        for (int f = 0; f < frames; f++)
        {
            var q = track.Rotations[f];
            if (MathF.Sqrt(q.Z * q.Z + q.W * q.W) < 1e-6f)
                throw Refuse($"bone '{name}' turns half a turn about a horizontal axis at frame {f}, so its turn about up " +
                             "has no direction. Refused rather than guessing the root motion.");

            float raw = 2f * MathF.Atan2(q.Z, q.W);
            turn = f == 0 ? Wrap(raw) : turn + Wrap(raw - turn);

            var upright = Quaternion.Normalize(Quaternion.Conjugate(Quaternion.CreateFromAxisAngle(Vector3.UnitZ, turn)) * q);
            if (f > 0 && Quaternion.Dot(swing[f - 1], upright) < 0) upright = -upright;

            swing[f] = upright;
            turns[f] = turn;
            planar[f] = new Vector3(track.Translations[f].X, track.Translations[f].Y, 0);
            height[f] = track.Translations[f].Z;
        }

        var motion = new RootMotion.Motion { Duration = duration };
        var start = planar[0];
        var startTurn = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, turns[0]);
        for (int f = 0; f < frames; f++)
        {
            float relative = turns[f] - turns[0];
            motion.Samples.Add(new RootMotion.Sample(
                planar[f] - Vector3.Transform(start, Matrix4x4.CreateFromAxisAngle(Vector3.UnitZ, relative)), relative));
            track.Translations[f] = new Vector3(start.X, start.Y, height[f]);
            track.Rotations[f] = Quaternion.Normalize(startTurn * swing[f]);
        }
        return motion;
    }

    private static float Wrap(float angle) => angle - 2f * MathF.PI * MathF.Round(angle / (2f * MathF.PI));

    private static void Bake(GltfScene scene, HashSet<int> joints, int node,
                             Dictionary<string, GltfChannel> channels, HkxBonePose reference,
                             float frameRate, int frames, HkxTrackData track)
    {
        bool hasTranslation = channels.TryGetValue("translation", out var translationChannel);
        bool hasRotation = channels.TryGetValue("rotation", out var rotationChannel);
        bool hasScale = channels.TryGetValue("scale", out var scaleChannel);
        var bone = scene.Nodes[node];

        Quaternion? previous = null;
        for (int f = 0; f < frames; f++)
        {
            float time = f / frameRate;
            var translation = hasTranslation ? Vector3At(translationChannel!, time) : bone.Translation;
            var rotation = hasRotation ? QuaternionAt(rotationChannel!, time) : bone.Rotation;
            var scale = hasScale ? Vector3At(scaleChannel!, time) : bone.Scale;

            if (!HavokPose(scene, joints, node, translation, rotation, scale,
                           out var havokTranslation, out var havokRotation, out var havokScale))
                throw Refuse($"bone '{bone.Name}' has a degenerate transform at {time:F3} s.");

            if (previous is Quaternion last && Quaternion.Dot(last, havokRotation) < 0) havokRotation = -havokRotation;
            previous = havokRotation;

            track.Translations.Add(hasTranslation ? havokTranslation : reference.Translation);
            track.Rotations.Add(hasRotation ? havokRotation : reference.Rotation);
            track.Scales.Add(hasScale ? havokScale : reference.Scale);
        }
    }

    private static bool HavokPose(GltfScene scene, HashSet<int> joints, int node, Vector3 translation,
                                  Quaternion rotation, Vector3 scale, out Vector3 havokTranslation,
                                  out Quaternion havokRotation, out Vector3 havokScale)
    {
        var helpers = Matrix4x4.Identity;
        foreach (int helper in Ancestors(scene, node).TakeWhile(ancestor => !joints.Contains(ancestor)))
            helpers *= Local(scene.Nodes[helper].Translation, scene.Nodes[helper].Rotation, scene.Nodes[helper].Scale);

        var world = Local(translation, rotation, scale) * helpers;
        if (JointParent(scene, joints, node) < 0) world *= ToHavok;
        return Matrix4x4.Decompose(world, out havokScale, out havokRotation, out havokTranslation);
    }

    private static Matrix4x4 Local(Vector3 translation, Quaternion rotation, Vector3 scale) =>
        Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(Unit(rotation)) *
        Matrix4x4.CreateTranslation(translation / MetresPerUnit);

    private static IEnumerable<int> Ancestors(GltfScene scene, int node)
    {
        for (int parent = scene.Nodes[node].Parent; parent >= 0; parent = scene.Nodes[parent].Parent)
            yield return parent;
    }

    private static int JointParent(GltfScene scene, HashSet<int> joints, int node)
    {
        foreach (int ancestor in Ancestors(scene, node))
            if (joints.Contains(ancestor)) return ancestor;
        return -1;
    }

    private static HkxBonePose Reference(HkxSkeleton skeleton, int bone) =>
        bone < skeleton.ReferencePose.Count ? skeleton.ReferencePose[bone] : new HkxBonePose();

    private static Vector3 Vector3At(GltfChannel channel, float time)
    {
        var (before, after, fraction) = Locate(channel.Times, time);
        if (channel.Interpolation == "CUBICSPLINE")
        {
            var spline = Spline(channel, 3, before, after, fraction);
            return new Vector3(spline[0], spline[1], spline[2]);
        }

        Vector3 At(int k) => new(channel.Values[k * 3], channel.Values[k * 3 + 1], channel.Values[k * 3 + 2]);
        return channel.Interpolation == "STEP" ? At(before) : Vector3.Lerp(At(before), At(after), fraction);
    }

    private static Quaternion QuaternionAt(GltfChannel channel, float time)
    {
        var (before, after, fraction) = Locate(channel.Times, time);
        if (channel.Interpolation == "CUBICSPLINE")
        {
            var spline = Spline(channel, 4, before, after, fraction);
            return Unit(new Quaternion(spline[0], spline[1], spline[2], spline[3]));
        }

        Quaternion At(int k) => Unit(new Quaternion(channel.Values[k * 4], channel.Values[k * 4 + 1],
                                                    channel.Values[k * 4 + 2], channel.Values[k * 4 + 3]));
        if (channel.Interpolation == "STEP") return At(before);

        var end = At(after);
        if (Quaternion.Dot(At(before), end) < 0) end = -end;
        return Unit(Quaternion.Slerp(At(before), end, fraction));
    }

    private static float[] Spline(GltfChannel channel, int width, int before, int after, float fraction)
    {
        float gap = channel.Times[after] - channel.Times[before];
        float[] Element(int index) => channel.Values[(index * width)..((index + 1) * width)];

        var p0 = Element(3 * before + 1);
        var m0 = Element(3 * before + 2);
        var p1 = Element(3 * after + 1);
        var m1 = Element(3 * after);
        if (width == 4 && Enumerable.Range(0, 4).Sum(c => p0[c] * p1[c]) < 0)
        {
            p1 = p1.Select(x => -x).ToArray();
            m1 = m1.Select(x => -x).ToArray();
        }

        float s = fraction, s2 = s * s, s3 = s2 * s;
        float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
        return Enumerable.Range(0, width)
            .Select(c => h00 * p0[c] + h10 * gap * m0[c] + h01 * p1[c] + h11 * gap * m1[c])
            .ToArray();
    }

    private static (int Before, int After, float Fraction) Locate(float[] times, float time)
    {
        int last = times.Length - 1;
        if (time <= times[0]) return (0, 0, 0f);
        if (time >= times[last]) return (last, last, 0f);

        int i = Array.BinarySearch(times, time);
        if (i >= 0) return (i, i, 0f);

        i = ~i - 1;
        return (i, i + 1, (time - times[i]) / (times[i + 1] - times[i]));
    }

    private static (float Position, float Rotation, float Scale) Measure(HkxAnimationData intended,
                                                                         HkxAnimationData decoded)
    {
        if (decoded.Tracks.Count != intended.Tracks.Count)
            throw Refuse($"the written clip reads back with {decoded.Tracks.Count} track(s), not " +
                         $"{intended.Tracks.Count}, so nothing was written.");

        float position = 0, rotation = 0, scale = 0;
        for (int t = 0; t < intended.Tracks.Count; t++)
        {
            var a = intended.Tracks[t];
            var b = decoded.Tracks[t];
            if (b.Translations.Count != intended.NumFrames || b.Rotations.Count != intended.NumFrames ||
                b.Scales.Count != intended.NumFrames)
                throw Refuse($"track {t} reads back with a different frame count, so nothing was written.");

            for (int f = 0; f < intended.NumFrames; f++)
            {
                position = MathF.Max(position, (a.Translations[f] - b.Translations[f]).Length());
                rotation = MathF.Max(rotation, Angle(a.Rotations[f], b.Rotations[f]));
                scale = MathF.Max(scale, (a.Scales[f] - b.Scales[f]).Length());
            }
        }
        return (position, rotation, scale);
    }

    private static float Angle(Quaternion a, Quaternion b)
    {
        var relative = Quaternion.Conjugate(Unit(a)) * Unit(b);
        float vector = MathF.Sqrt(relative.X * relative.X + relative.Y * relative.Y + relative.Z * relative.Z);
        return 2f * MathF.Atan2(vector, MathF.Abs(relative.W));
    }

    private static Quaternion Unit(Quaternion q) =>
        q.LengthSquared() > 1e-12f ? Quaternion.Normalize(q) : Quaternion.Identity;

    private static InvalidOperationException Refuse(string reason) => new(reason);
}
