using System.Numerics;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

public partial class MainWindow
{
    private Vector3 _rootCameraCentre;
    private Vector3 _rootLoopOffset;
    private int _lastRootFrame;

    private void ResetRootMotionView()
    {
        _rootCameraCentre = _skeleton.ViewCentre;
        ResetRootTravelLoop();
        _lastRootFrame = 0;
        UpdateRootMotionView();
    }

    private void ResetRootTravelLoop()
    {
        _rootLoopOffset = Vector3.Zero;
    }

    private void UpdateRootMotionView()
    {
        var position = RootMotion.AtTime(_poseMotion, _playback.Time).Position;
        if (_playback.IsPlaying && _playback.Frame < _lastRootFrame && !_followTravel && _poseMotion.Previewable)
            _rootLoopOffset -= _poseMotion.Travel;
        var origin = RootMotion.AtTime(_poseMotion, 0).Position;
        var fixedCentre = _rootCameraCentre + _rootLoopOffset;
        _skeleton.SetTravelCentre(_followTravel ? _rootCameraCentre + position - origin : fixedCentre);
        _skeleton.ShowRootMotion(_poseMotion, Vector3.Zero);
        _lastRootFrame = _playback.Frame;
    }

    public void FollowTravelForTest(bool follow)
    {
        _travelToggle.IsChecked = follow;
        ShowFrame(_playback.Frame, stop: true);
    }

    public void PlayFramesForTest(int frames)
    {
        if (!_playback.Start(1f)) return;
        for (int i = 0; i < frames; i++) ShowFrame(_playback.Tick(), stop: false);
    }

    public AnimationPose.Pose? PlacedPoseForTest => _poseSkeleton == null ? null
        : WithTravel(AnimationPose.At(_poseSkeleton, _poseAnimation, _playback.Frame));
}
