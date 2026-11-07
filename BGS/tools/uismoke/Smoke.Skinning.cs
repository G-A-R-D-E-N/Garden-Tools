using System;
using System.IO;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Archive;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{
    public static int SkinningSmoke()
    {
        string? folder = Environment.GetEnvironmentVariable("BGS_SKIN_DATA");
        if (string.IsNullOrEmpty(folder) || !Directory.Exists(folder)) return 0;
        int before = FailedCount;
        const string animation = "Meshes/Actors/Character/Animations/MT/Neutral/Stand_to_Walk.hkx";
        const string assets = "Meshes/Actors/Character/CharacterAssets/";
        string root = Directory.CreateTempSubdirectory("bgs-skin-ui-").FullName;
        try
        {
            using (var data = GameData.Discover(folder))
            {
                foreach (string name in new[] { "MaleBody.nif", "FemaleBody.nif", "skeleton.hkx", "skeleton.nif" })
                {
                    string archive = Path.Combine(folder, name.EndsWith(".hkx", StringComparison.Ordinal)
                        ? "Fallout4 - Animations.ba2" : "Fallout4 - Meshes.ba2");
                    File.WriteAllBytes(Path.Combine(root, name), data.ReadEntry(archive, assets + name)!);
                }
                File.WriteAllBytes(Path.Combine(root, "walk.hkx"), data.ReadEntry(
                    Path.Combine(folder, "Fallout4 - Animations.ba2"), animation)!);
            }
            WithTemporarySettings(settingsPath =>
            {
                Settings.TrySet("gameDataFolder", folder, out _);
                Settings.TrySet("gameModsFolder", "", out _);
                var window = new MainWindow();
                window.Show();
                try
                {
                    SelectTab(window, "Animations");
                    CheckTrue("skin animation opens packed", window.OpenArchiveForTest(
                        Path.Combine(folder, "Fallout4 - Animations.ba2"), animation));
                    Dispatcher.UIThread.RunJobs();
                    window.OpenMesh(Path.Combine(root, "MaleBody.nif"));
                    CheckTrue("MaleBody resolves all 58 bones with source counts", window.PlaybackSummary.Contains("58 of 58 bones: 13 from the Havok skeleton, 45 from skeleton.nif", StringComparison.Ordinal));
                    window.ScrubTo(12);
                    Check("skin frame can be scrubbed", 12, window.PoseFrame);
                    File.WriteAllBytes(Path.Combine(root, "skeleton.nif"), new byte[] { 0, 1, 2 });
                    window.LoadPoseFrom(Path.Combine(root, "walk.hkx"));
                    CheckTrue("mesh then animation rebinds and malformed skeleton tree falls back to body", window.PlaybackSummary.Contains("58 of 58 bones: 13 from the Havok skeleton, 45 from body NIF", StringComparison.Ordinal));
                    window.OpenMesh(Path.Combine(root, "FemaleBody.nif"));
                    CheckTrue("changing mesh binds the replacement", window.PlaybackSummary.Contains("FemaleBody.nif", StringComparison.Ordinal) && window.PlaybackSummary.Contains("58 of 58 bones", StringComparison.Ordinal));
                }
                finally { CloseForTest(window); }
                var reverse = new MainWindow();
                reverse.Show();
                try
                {
                    reverse.OpenMesh(Path.Combine(root, "MaleBody.nif"));
                    reverse.LoadPoseFrom(Path.Combine(root, "walk.hkx"));
                    CheckTrue("mesh chosen before first animation binds when skeleton arrives", reverse.PlaybackSummary.Contains("58 of 58 bones", StringComparison.Ordinal));
                }
                finally { CloseForTest(reverse); }
            });
        }
        finally { Directory.Delete(root, true); }
        return FailedCount - before;
    }
}
