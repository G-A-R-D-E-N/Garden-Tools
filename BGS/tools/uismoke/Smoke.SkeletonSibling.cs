using System;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{
    internal static int SkeletonSiblingSmoke()
    {
        Console.WriteLine("\na packed sibling skeleton wins over another creature's copy in the shared extract folder");

        int before = FailedCount;
        string samples = Path.Combine(AppContext.BaseDirectory, "samples");
        string clipSample = Path.Combine(samples, "TurretIdleWeapReady.hkx");
        string standingSample = Path.Combine(samples, "TurretStandingSkeleton.hkx");
        string mountedSample = Path.Combine(samples, "TurretMountedSkeleton.hkx");
        if (!File.Exists(clipSample) || !File.Exists(standingSample) || !File.Exists(mountedSample))
        {
            Console.WriteLine("        skeleton sibling: skipped, the genuine samples are not here");
            return 0;
        }

        var reader = new HkxBinaryReader();
        HkxSkeleton? standing = ReadSkeletonOrNull(reader, standingSample);
        HkxSkeleton? mounted = ReadSkeletonOrNull(reader, mountedSample);
        CheckTrue("the two genuine skeletons differ", standing != null && mounted != null && !Same(standing, mounted));
        if (standing == null || mounted == null) return 0;

        const string clipPath = "Meshes/Actors/Turret/Animations/Mounted/Idle_WeapReady.hkx";
        const string skeletonPath = "Meshes/Actors/Turret/CharacterAssets/Skeleton.hkx";

        string root = Directory.CreateTempSubdirectory("bgs-skeleton-sibling-").FullName;
        string? previous = Settings.SettingsPathForTest;
        Settings.SettingsPathForTest = Path.Combine(Path.GetTempPath(), $"bgs-sibling-{Guid.NewGuid():N}.cfg");
        Settings.TrySet("tour_done", "1", out _);

        try
        {
            string archive = PackData(Path.Combine(root, "Data"), clipPath, skeletonPath, clipSample, standingSample);
            Settings.TrySet("gameDataFolder", Path.GetDirectoryName(archive) ?? "", out _);

            string folder = Path.Combine(Path.GetTempPath(), "BehaviourGraphStudio", MainWindow.TempDirKey(archive));
            Directory.CreateDirectory(folder);
            string clipCopy = Path.Combine(folder, "Meshes_Actors_Turret_Animations_Mounted_Idle_WeapReady.hkx");
            File.WriteAllBytes(clipCopy, File.ReadAllBytes(clipSample));
            File.WriteAllBytes(Path.Combine(folder, "TurretMountedSkeleton.hkx"), File.ReadAllBytes(mountedSample));
            File.WriteAllBytes(Path.Combine(folder, "TurretStandingSkeleton.hkx"), File.ReadAllBytes(standingSample));

            string? onDisk = MainWindow.SiblingSkeletonPath(clipCopy);
            CheckTrue("another creature's skeleton is what the shared folder would offer",
                      onDisk != null && !Same(ReadSkeletonOrNull(reader, onDisk), standing));

            var direct = new MainWindow();
            direct.Show();
            try
            {
                HkxSkeleton? chosen = direct.ResolveAnimationSkeleton(
                    clipCopy, clipPath, Path.GetDirectoryName(archive));
                CheckTrue("an archive clip with a virtual path resolves its packed sibling first",
                          Same(chosen, standing));
                CheckTrue("and passes over the other creature's copy", chosen != null && !Same(chosen, mounted));
            }
            finally { CloseForTest(direct); }

            string owned = Path.Combine(root, "DataOpen");
            string ownedArchive = PackData(owned, clipPath, skeletonPath, clipSample, standingSample);
            Settings.TrySet("gameDataFolder", owned, out _);

            var window = new MainWindow();
            window.Show();
            try
            {
                MainWindow.DataFolderDiscoveriesForTest = 0;
                CheckTrue("the archive browser opens the packed clip",
                          window.OpenArchiveForTest(ownedArchive, clipPath));
                Dispatcher.UIThread.RunJobs();

                CheckTrue("the open poses the clip", window.PoseAnimationLoaded && window.PoseFrameCount > 1);
                CheckTrue("the pose uses the packed sibling skeleton",
                          Same(window.PoseSkeletonForTest, standing));
                CheckTrue("the Animation tab names the packed sibling skeleton",
                          window.AnimationSummary.Contains($"sibling skeleton of {standing.BoneNames.Count}",
                                                           StringComparison.Ordinal));
                Check("discovery runs once for the whole load", 1, MainWindow.DataFolderDiscoveriesForTest);
            }
            finally { CloseForTest(window); }

            CheckTrue("the open released the archives it discovered", DeleteTree(owned));

            string hint = Path.Combine(root, "Hint");
            Directory.CreateDirectory(hint);
            WriteArchive(Path.Combine(hint, "Hint - Extra.ba2"),
                         new[] { (clipPath, File.ReadAllBytes(clipSample)) });
            Settings.TrySet("gameDataFolder", Path.GetDirectoryName(archive) ?? "", out _);

            var hinted = new MainWindow();
            hinted.Show();
            try
            {
                HkxSkeleton? fallback = hinted.ResolveAnimationSkeleton(clipCopy, clipPath, hint);
                CheckTrue("a hint folder without the entry falls back to the game data folder",
                          Same(fallback, standing));
            }
            finally { CloseForTest(hinted); }
        }
        finally
        {
            Settings.SettingsPathForTest = previous;
            DeleteTree(root);
        }

        return FailedCount == before ? 0 : 1;
    }

    private static string PackData(string data, string clipPath, string skeletonPath, string clip, string skeleton)
    {
        Directory.CreateDirectory(data);
        string archive = Path.Combine(data, "Fallout4 - Animations.ba2");
        WriteArchive(archive, new[]
        {
            (clipPath, File.ReadAllBytes(clip)),
            (skeletonPath, File.ReadAllBytes(skeleton)),
        });
        return archive;
    }

    private static bool DeleteTree(string path)
    {
        try { Directory.Delete(path, true); return true; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return false; }
    }

    private static bool Same(HkxSkeleton? left, HkxSkeleton? right)
    {
        if (left == null || right == null) return left == right;
        return left.BoneNames.Count == right.BoneNames.Count &&
               string.Equals(left.Name, right.Name, StringComparison.Ordinal) &&
               left.BoneNames.SequenceEqual(right.BoneNames, StringComparer.Ordinal);
    }

    private static HkxSkeleton? ReadSkeletonOrNull(HkxBinaryReader reader, string path)
    {
        try { return reader.ReadSkeleton(path); }
        catch (Exception) { return null; }
    }
}
