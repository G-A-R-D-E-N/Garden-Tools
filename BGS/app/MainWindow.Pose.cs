using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.VisualTree;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using OpenCommonwealth.Services;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    private async Task OpenFromArchive()
    {
        var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Which archive to look in",
            AllowMultiple = false,
            SuggestedStartLocation = await StartFolder(),
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Bethesda archives") { Patterns = new[] { "*.ba2", "*.BA2" } },
                FilePickerFileTypes.All,
            },
        });

        string? archivePath = picked.Count > 0 ? picked[0].TryGetLocalPath() : null;
        if (archivePath == null) return;

        await OpenArchive(archivePath);
    }

    private async Task OpenArchive(string archivePath)
    {

        OpenCommonwealth.Services.Archive.Ba2 archive;
        try
        {
            archive = OpenCommonwealth.Services.Archive.Ba2.Open(archivePath);
        }
        catch (Exception e)
        {
            SetStatus("That archive could not be read: " + e.Message, Ux.BadBrush);
            return;
        }

        using (archive)
        {
            var browser = new ArchiveBrowser(archive, ".hkx");
            await browser.ShowDialog(this);
            if (browser.Chosen is not { } entry) return;

            OpenArchiveEntry(archivePath, archive, entry);
        }
    }

    internal bool OpenArchiveForTest(string archivePath, string entryName)
    {
        OpenCommonwealth.Services.Archive.Ba2 archive;
        try { archive = OpenCommonwealth.Services.Archive.Ba2.Open(archivePath); }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            SetStatus("That archive could not be read: " + e.Message, Ux.BadBrush);
            return false;
        }

        using (archive)
        {
            OpenCommonwealth.Services.Archive.Ba2.Entry? entry = archive.Entries.FirstOrDefault(
                candidate => candidate.Name.Equals(entryName, StringComparison.OrdinalIgnoreCase));
            if (entry == null)
            {
                SetStatus($"{entryName} is not inside {Path.GetFileName(archivePath)}.", Ux.BadBrush);
                return false;
            }

            OpenArchiveEntry(archivePath, archive, entry);
            return true;
        }
    }

    private void OpenArchiveEntry(string archivePath, OpenCommonwealth.Services.Archive.Ba2 archive,
                                  OpenCommonwealth.Services.Archive.Ba2.Entry entry)
    {
        try
        {

            string folder = Path.Combine(Path.GetTempPath(), "BehaviourGraphStudio",
                                         TempDirKey(archivePath));
            Directory.CreateDirectory(folder);

            string copy = Path.Combine(folder, entry.Name.Replace('/', '_'));
            File.WriteAllBytes(copy, archive.Read(entry));

            _pendingVirtualPathHint = entry.Name;
            _pendingSourceFolder = Path.GetDirectoryName(Path.GetFullPath(archivePath)) ?? "";
            _pathField.Text = copy;
            if (!Load()) return;

            _readOnly = true;
            _readOnlyWhy = $"{entry.FileName} came out of {Path.GetFileName(archivePath)}, and " +
                           "nothing here writes back into an archive. Save a copy somewhere of " +
                           "your own and open that to edit it.";
            RefreshSaveAnimationState();
            SetStatus($"Opened {entry.Name} from {Path.GetFileName(archivePath)}, read only. " +
                      $"The copy is at {copy}", Ux.MetaBrush);
        }
        catch (Exception e)
        {
            SetStatus($"Could not open {entry.FileName} from the archive: " + e.Message, Ux.BadBrush);
        }
    }

    private async Task PickMesh()
    {
        var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Which mesh to draw on this skeleton",
            AllowMultiple = false,
            SuggestedStartLocation = await StartFolder(),
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Meshes") { Patterns = new[] { "*.nif", "*.NIF" } },
                FilePickerFileTypes.All,
            },
        });

        string? path = picked.Count > 0 ? picked[0].TryGetLocalPath() : null;
        if (path != null) LoadMesh(path);
    }

    private bool LoadMesh(string path)
    {
        var skeleton = PoseSkeleton();
        if (skeleton == null)
        {
            SetPlaybackSummary("No skeleton is resolved for this file, so a mesh has nothing to hang on.",
                               Ux.BadBrush);
            return false;
        }

        ClearMesh();
        int approximated = 0;
        try
        {
            var nif = NifFile.Read(path);
            foreach (var shape in NifGeometry.Shapes(nif))
            {
                approximated += ApproximateHelperBoneInterim(shape, skeleton);
                var binding = SkinnedMesh.Bind(shape, skeleton);
                _meshShapes.Add((shape, binding, SkinnedMesh.Edges(shape)));
                int[] indices = shape.Indices.ToArray();
                _meshTriangles.Add(indices);
                _meshParts.Add(new MeshPart { Indices = indices });
                _meshSkins.Add(new SkinnedMesh.SkinScratch());
            }
        }
        catch (Exception ex)
        {
            ClearMesh();
            SetPlaybackSummary($"Could not read {Path.GetFileName(path)}: {ex.Message.Split('\n')[0]}",
                               Ux.BadBrush);
            return false;
        }

        if (_meshShapes.Count == 0)
        {
            SetPlaybackSummary($"{Path.GetFileName(path)} holds no drawable shape.", Ux.MutedBrush);
            return false;
        }

        _meshPath = path;
        string? settingsWarning = RememberSetting("last_mesh_folder", Path.GetDirectoryName(path) ?? "",
                                                  "The mesh loaded");

        int vertices = _meshShapes.Sum(m => m.Shape.Vertices.Count);
        int edges = _meshShapes.Sum(m => m.Edges.Count);

        var missing = _meshShapes.SelectMany(m => m.Binding.Unmatched).Distinct().ToList();
        float drift = _meshShapes.Max(m => SkinnedMesh.BindError(m.Shape, m.Binding, skeleton));

        string report = $"{Path.GetFileName(path)}   {_meshShapes.Count} shapes, {vertices} vertices, " +
                        $"{edges} edges   drift from the rest pose {drift:F2}";
        if (missing.Count > 0)
            report += $"   {missing.Count} bone{(missing.Count == 1 ? "" : "s")} did not match this " +
                      $"skeleton: {string.Join(", ", missing.Take(6))}" +
                      (missing.Count > 6 ? ", and more" : "") +
                      ". Those bones keep their whole weight, so a vertex weighted only to them follows " +
                      "this frame's placement of the bones that did match.";
        if (approximated > 0)
            report += $"   {approximated} helper bone{(approximated == 1 ? "" : "s")} approximated by " +
                      "their parent bone.";

        SetPlaybackSummary(settingsWarning ?? report,
                           settingsWarning == null && missing.Count == 0 ? Ux.MetaBrush : Ux.WarnBrush);
        QuietOverlaysForMesh();
        ShowFrame(_playback.Frame, stop: false);
        _skeleton.Frame();
        return true;
    }

    internal static int ApproximateHelperBoneInterim(NifShape shape, HkxSkeleton skeleton)
    {
        var known = new HashSet<string>(skeleton.BoneNames, StringComparer.OrdinalIgnoreCase);
        int approximated = 0;
        for (int i = 0; i < shape.BoneNames.Count; i++)
        {
            string name = shape.BoneNames[i];
            string resolved = SkinnedMesh.ResolveSkinBoneName(name, known.Contains);
            if (resolved == name) continue;

            shape.BoneNames[i] = resolved;
            approximated++;
        }

        return approximated;
    }
    private void ClearMesh()
    {
        _meshShapes.Clear();
        _meshTriangles.Clear();
        _meshParts.Clear();
        _meshSkins.Clear();
        _meshSegmentCount = 0;
        _meshPath = "";
        _skeleton.ShowMesh(null);
        SetViewportMesh(null);
    }

    private void UpdateMesh(AnimationPose.Pose pose, HkxSkeleton skeleton)
    {
        if (_meshShapes.Count == 0)
        {
            _skeleton.ShowMesh(null);
            SetViewportMesh(null);
            return;
        }

        _meshSkinClock.Restart();
        int segmentAt = 0;

        for (int i = 0; i < _meshShapes.Count; i++)
        {
            var (shape, binding, edges) = _meshShapes[i];
            var part = _meshParts[i];
            part.Positions = SkinnedMesh.Pose(shape, binding, pose, skeleton, part.Positions, _meshSkins[i]);

            if (segmentAt + edges.Count > _meshSegments.Length)
                Array.Resize(ref _meshSegments, Math.Max(64, (segmentAt + edges.Count) * 2));

            foreach (var (from, to) in edges)
                _meshSegments[segmentAt++] = (part.Positions[from], part.Positions[to]);
        }

        _meshSegmentCount = segmentAt;
        _meshSkinMilliseconds = _meshSkinClock.Elapsed.TotalMilliseconds;
        _skeleton.ShowMesh(_meshSegments, _meshSegmentCount);
        UpdateViewportGeometry();
    }

    private void ClearPose()
    {
        Stop();
        _poseAnimation = null;
        _poseSkeleton = null;
        _poseSource = "";
        _poseRefusal = "";
        _playback.Clear();
        UpdateTransportState();

        _ragdoll = null;
        _ragdollSourceName = null;
        _ragdollSourceNote = null;
        _ragdollSiblingName = null;
        ClearPhysicsReport();

        _overlayDefaultsApplying = true;
        try
        {
            foreach (var toggle in new[]
                     { _bodiesToggle, _constraintsToggle, _bindingsToggle,
                       _physicsSkeletonToggle, _framesToggle, _engineerToggle })
            {
                toggle.IsChecked = false;
                toggle.IsEnabled = false;
            }
        }
        finally
        {
            _overlayDefaultsApplying = false;
        }
        _dropButton.IsEnabled = false;

        _poseMotion = new RootMotion.Motion();
        _cachedSkeleton = null;
        _cachedSkeletonFor = "";
        _scrubbing = true;
        _scrub.Maximum = 0;
        _scrub.Value = 0;
        _scrubbing = false;
        _dropClock.Stop();
        _dropButton.Content = "Drop";
        _frameLabel.Text = "";
        _skeleton.Reset();
        UpdateScaleStatus();

        SetPlaybackSummary("Open a behaviour and select a clip to see what it plays. That animates " +
                           "the skeleton; use Mesh... to hang a model on it.", Ux.MutedBrush);
    }

    private Control WithClipPicker(Control viewport)
    {
        _clips.SelectionChanged += () =>
        {
            if (_clips.SelectedTag is not string id || id == _selectedId) return;
            var model = Model();
            ShowProps(id, model);
            LoadPoseFromSelection(announce: true);
        };

        var right = new Grid();
        right.RowDefinitions.Add(new RowDefinition(new GridLength(2, GridUnitType.Star)));
        right.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        right.RowDefinitions.Add(new RowDefinition(new GridLength(3, GridUnitType.Star)));

        var horizontal = new GridSplitter { Height = 6, Background = Brushes.Transparent };
        Grid.SetRow(horizontal, 1);
        Grid.SetRow(_clipProps, 2);
        right.Children.Add(_clips);
        right.Children.Add(horizontal);
        right.Children.Add(_clipProps);

        var split = new Grid();
        split.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(3, GridUnitType.Star)));
        split.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
        split.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(360, GridUnitType.Pixel)));

        var splitter = new GridSplitter { Width = 6, Background = Brushes.Transparent };
        Grid.SetColumn(splitter, 1);
        Grid.SetColumn(right, 2);
        split.Children.Add(viewport);
        split.Children.Add(splitter);
        split.Children.Add(right);
        return split;
    }

    private void FindMeshForFile()
    {
        if (_meshPath.Length > 0) return;

        var found = MeshLookup.Find(_hkxPath, _projectChain?.Root, _projectChain?.SkeletonPath);
        if (!found.Found)
        {
            if (_poseAnimation == null)
                SetPlaybackSummary("Select a clip to see what it plays. " + found.Reason, Ux.MutedBrush);
            return;
        }

        if (!LoadMesh(found.Path!)) return;

        if (_poseAnimation == null)
            SetPlaybackSummary($"Select a clip to see what it plays, on {Path.GetFileName(found.Path!)}.",
                               Ux.MutedBrush);
    }

    private void BuildClipList(BehaviourGraphModel model)
    {
        _clips.Clear();
        foreach (var clip in model.Objects.Where(o => o.Class == "hkbClipGenerator"))
        {
            string animation = clip.Str("animationName");
            _clips.Add(null, clip.Str("name"), animation.Length > 0 ? animation : "nothing")
                  .Colour(0, Ux.TitleBrush)
                  .Colour(1, animation.Length > 0 ? Ux.CodeBrush : Ux.MutedBrush)
                  .Tag(clip.Id);
        }

        if (_clips.RowCount == 0) ShowLoneAnimation();
    }

    private void ShowLoneAnimation()
    {
        if (_animationData is not { NumFrames: > 0 }) return;

        _clips.Add(null, Path.GetFileNameWithoutExtension(_hkxPath),
                   $"{_animationData.Duration:F2}s, {_animationData.NumFrames} frames")
              .Colour(0, Ux.TitleBrush)
              .Colour(1, Ux.CodeBrush);

        _clips.SelectFirst();
    }

    private void TogglePlay()
    {
        if (_poseAnimation == null || !_playback.CanPlay)
        {
            SetPlaybackSummary(PoseRefusal(), Ux.MutedBrush);
            return;
        }

        if (_playback.IsPlaying || _clock != null) { Stop(); return; }
        if (!_playback.Start(SelectedPlaybackSpeed())) return;

        _clock = new DispatcherTimer { Interval = _playback.Interval };
        _clock.Tick += (_, _) => ShowFrame(_playback.Tick(), stop: false);
        _clock.Start();
        _playButton.Content = "Pause";
    }

    private float SelectedPlaybackSpeed()
    {
        if (_xmlText.Length == 0 || _selectedId.Length == 0) return 1f;

        foreach (var p in HkxTextEdit.ReadParams(_xmlText, _selectedId))
            if (p.Name == "playbackSpeed" && float.TryParse(p.Value, out float speed))
                return speed > 0f ? speed : 1f;

        return 1f;
    }

    private void Stop()
    {
        _clock?.Stop();
        _clock = null;
        _playback.Stop();
        _playButton.Content = "Play";
        UpdateTransportState();
    }

    internal string PoseRefusal() => _poseRefusal.Length > 0
        ? _poseRefusal + "  There is nothing posable to play or step."
        : "Nothing loaded to play. Select a clip, or press From selected node.";

    private void ShowFrame(int frame, bool stop)
    {
        if (stop) Stop();
        if (_poseAnimation == null || _poseSkeleton == null) return;

        _playback.Show(frame);
        var posed = AnimationPose.At(_poseSkeleton, _poseAnimation, _playback.Frame);
        if (_followTravel) posed = WithTravel(posed);

        _skeleton.Update(posed);
        UpdateMesh(posed, _poseSkeleton);

        _scrubbing = true;
        _scrub.Value = _playback.Frame;
        _scrubbing = false;
        UpdateFrameLabel();
    }

    private AnimationPose.Pose WithTravel(AnimationPose.Pose pose)
    {
        if (!_poseMotion.Any || _poseAnimation == null) return pose;

        float fraction = _playback.Fraction;

        var at = RootMotion.At(_poseMotion, fraction);
        var turn = System.Numerics.Quaternion.CreateFromAxisAngle(System.Numerics.Vector3.Normalize(_poseMotion.Up), at.TurnRadians);

        var moved = new AnimationPose.Pose { Frame = pose.Frame, Time = pose.Time };
        moved.Links.AddRange(pose.Links);

        foreach (var bone in pose.Bones)
        {
            var position = System.Numerics.Vector3.Transform(bone.Position, turn) + at.Position;
            moved.Bones.Add(bone with { Position = position, Rotation = turn * bone.Rotation });
            moved.Min = System.Numerics.Vector3.Min(moved.Min, position);
            moved.Max = System.Numerics.Vector3.Max(moved.Max, position);
        }

        return moved;
    }

    private void UpdateFrameLabel()
    {
        _frameLabel.Text = _poseAnimation == null
            ? ""
            : $"frame {_playback.Frame} of {_playback.LastFrame}   " +
              $"{_playback.Time:F3}s   fraction {_playback.Fraction:0.###}";
    }

    private void SetPlaybackSummary(string text, IBrush brush)
    {
        _summaryBaseText = text;
        _summaryBaseBrush = brush;
        _playbackSummary.Text = text;
        _playbackSummary.Foreground = brush;
    }

    private void ShowConstraintInspection(string? line)
    {
        _playbackSummary.Text = line ?? _summaryBaseText;
        _playbackSummary.Foreground = line == null ? _summaryBaseBrush : Ux.WarnBrush;
    }

    public SkeletonView Viewport => _skeleton;
    public int PoseFrame => _playback.Frame;
    public int PoseFrameCount => _playback.FrameCount;
    public string PlaybackSummary => _playbackSummary.Text ?? "";
    public bool IsPlaying => _playback.IsPlaying;

    public void ScrubTo(int frame) => ShowFrame(frame, stop: true);
    public void LoadPoseFrom(string animationPath) => LoadPose(animationPath, Path.GetFileName(animationPath));
    public bool PoseAnimationLoaded => _poseAnimation != null;
    public string PoseSourceForTest => _poseSource;

    public HkxSkeleton? PoseSkeletonForTest => _poseSkeleton;
    public string PoseRefusalForTest => _poseRefusal;
    public bool TransportEnabledForTest => _playButton.IsEnabled;
    public bool PlayButtonEnabledForTest => _playButton.IsEnabled;
    public bool StepButtonsEnabledForTest => _forwardFrameButton.IsEnabled;
    public void PlayForTest() => TogglePlay();
    public void StepForwardForTest() => ShowFrame(_playback.Frame + 1, stop: true);
    public AnimationPose.Pose? PoseNow =>
        _poseSkeleton == null ? null : AnimationPose.At(_poseSkeleton, _poseAnimation, _playback.Frame);

}
