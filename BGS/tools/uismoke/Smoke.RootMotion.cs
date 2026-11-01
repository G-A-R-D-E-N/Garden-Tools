using System.IO;
using System.Numerics;
using System.Reflection;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{
    internal static int RootMotionSmoke()
    {
        int before = _failed;
        string file = Path.Combine("tools", "tests", "fixtures", "vanilla", "Meshes", "Actors", "Character",
            "Animations", "Paired", "PairedKill2HMBashKneeAndHead_AttackerLead.hkx");
        var motion = RootMotion.Read(file);
        CheckTrue("root travel fixture has a meaningful path", motion.Previewable && motion.Distance > 10);
        var window = new MainWindow();
        window.Show();
        try
        {
            window.Open(file);
            Dispatcher.UIThread.RunJobs();
            CheckTrue("root travel animation loads", window.PoseAnimationLoaded);
            Vector3 Centre() => (Vector3)typeof(SkeletonView)
                .GetProperty("ViewCentre", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window.Viewport)!;
            Vector3 Root() => window.PlacedPoseForTest!.Bones[0].Position;
            window.FollowTravelForTest(false);
            window.ScrubTo(0);
            Vector3 startCentre = Centre();
            Vector3 startRoot = Root();
            window.ScrubTo(window.PoseFrameCount - 1);
            Vector3 offCentre = Centre();
            Vector3 offRoot = Root();
            CheckTrue("Follow travel OFF leaves the camera fixed", Vector3.Distance(startCentre, offCentre) < 0.001f);
            CheckTrue("Follow travel OFF moves the placed skeleton", Vector3.Distance(startRoot, offRoot) > 10);
            window.FollowTravelForTest(true);
            Vector3 onCentre = Centre();
            CheckTrue("Follow travel ON preserves world placement", Vector3.Distance(offRoot, Root()) < 0.001f);
            CheckTrue("Follow travel ON holds root travel in the viewport",
                Vector3.Distance(onCentre - startCentre, motion.Travel) < 0.01f);
            window.ScrubTo(0);
            CheckTrue("Follow travel ON returns camera with the clip", Vector3.Distance(Centre(), startCentre) < 0.001f);
            CheckTrue("Follow travel ON returns placed skeleton", Vector3.Distance(Root(), startRoot) < 0.001f);
        }
        finally { window.Close(); Dispatcher.UIThread.RunJobs(); }
        return _failed - before;
    }

    internal static int RootMotionLoopSmoke()
    {
        int before = _failed;
        string file = Path.Combine("tools", "tests", "fixtures", "vanilla", "Meshes", "Actors", "Character",
            "Animations", "Paired", "PairedKill2HMBashKneeAndHead_AttackerLead.hkx");
        var motion = RootMotion.Read(file);
        CheckTrue("loop smoke fixture travels far enough to see the wrap",
            motion.Previewable && motion.Travel.Length() > 1);
        var window = new MainWindow();
        window.Show();
        try
        {
            window.Open(file);
            Dispatcher.UIThread.RunJobs();
            CheckTrue("loop smoke animation loads", window.PoseAnimationLoaded);
            Vector3 Centre() => (Vector3)typeof(SkeletonView)
                .GetProperty("ViewCentre", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window.Viewport)!;
            Vector3 Root() => window.PlacedPoseForTest!.Bones[0].Position;
            window.FollowTravelForTest(false);
            window.ScrubTo(0);
            Vector3 startCentre = Centre();
            Vector3 startRoot = Root();
            window.PlayFramesForTest(window.PoseFrameCount + 7);
            CheckTrue("playing across the wrap carries the fixed camera back by the clip travel",
                Vector3.Distance(Centre(), startCentre) > 1);
            window.ScrubTo(0);
            CheckTrue("scrubbing to frame 0 clears the loop offset",
                Vector3.Distance(Centre(), startCentre) < 0.001f);
            CheckTrue("and the camera is back on the frame-0 character",
                Vector3.Distance(Root(), startRoot) < 0.001f);
        }
        finally { window.Close(); Dispatcher.UIThread.RunJobs(); }
        return _failed - before;
    }
}
