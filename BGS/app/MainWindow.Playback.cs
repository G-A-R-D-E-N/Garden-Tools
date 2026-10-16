using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input.Platform;
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

internal readonly record struct RagdollChoice(HavokRagdollModel? Model, PhysicsLoadReport? Report,
                                              string ReportPath, string? SourcePath);

public partial class MainWindow : Window
{
    private Control BuildPlaybackTab()
    {
        _playButton.Click += (_, _) => TogglePlay();

        var first = Ux.Secondary("|<");
        first.Click += (_, _) => ShowFrame(0, stop: true);
        var back = Ux.Secondary("<");
        back.Click += (_, _) => ShowFrame(_playback.Frame - 1, stop: true);
        var forward = Ux.Secondary(">");
        forward.Click += (_, _) => ShowFrame(_playback.Frame + 1, stop: true);
        var last = Ux.Secondary(">|");
        last.Click += (_, _) => ShowFrame(int.MaxValue, stop: true);

        var fit = Ux.Secondary("Fit");
        fit.Click += (_, _) => _skeleton.Frame();

        _dropButton.IsEnabled = false;
        ToolTip.SetTip(_dropButton, "Release the current ragdoll pose (or the playing frame) and watch a " +
                                    "gravity-settled approximation of how it would drop - no solver. " +
                                    "Click again to return to the mapped pose.");
        _dropClock.Tick += (_, _) =>
        {
            _skeleton.AdvanceDrop(0.016f);
            if (_skeleton.DropResting) _dropClock.Stop();
        };
        _dropButton.Click += (_, _) =>
        {
            if (_skeleton.IsDropped)
            {
                _skeleton.ClearDrop();
                _dropClock.Stop();
            }
            else
            {
                _skeleton.StartDrop();
                if (_skeleton.IsDropped)
                {
                    Stop();
                    _dropClock.Start();
                }
            }
            _dropButton.Content = _skeleton.IsDropped ? "Recover" : "Drop";
        };

        var reference = new CheckBox
        {
            Content = "Reference pose",
            Foreground = Ux.MetaBrush,
            FontSize = 12,
            VerticalAlignment = VerticalAlignment.Center,
        };
        reference.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowReference = reference.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        var travel = new CheckBox
        {
            Content = "Follow travel",
            Foreground = Ux.MetaBrush,
            FontSize = 12,
            VerticalAlignment = VerticalAlignment.Center,
        };

        _bodiesToggle.IsEnabled = false;
        ToolTip.SetTip(_bodiesToggle, "Draw the rigid bodies this file measures (hknpRagdollData), " +
                                      "as a static file-space overlay on the skeleton.");
        _bodiesToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowBodies = _bodiesToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _constraintsToggle.IsEnabled = false;
        ToolTip.SetTip(_constraintsToggle, "Draw this file's measured ragdoll constraints: body links, " +
                                          "pivots (transformA/transformB) and the twist/cone/angle limits " +
                                          "the constraint atoms store.");
        _constraintsToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowConstraints = _constraintsToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _bindingsToggle.IsEnabled = false;
        ToolTip.SetTip(_bindingsToggle, "Label each bound rigid body with its bone name from the physics " +
                                        "skeleton (hknpRagdollData.boneToBodyMap). Hover a body to highlight " +
                                        "the body-to-bone association.");
        _bindingsToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowBindings = _bindingsToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _physicsSkeletonToggle.IsEnabled = false;
        ToolTip.SetTip(_physicsSkeletonToggle, "Draw the physics skeleton hknpRagdollData references (hkaSkeleton): " +
                                               "its bone chain and reference pose, composed down the parent " +
                                               "indices into file space.");
        _physicsSkeletonToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowPhysicsSkeleton = _physicsSkeletonToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _framesToggle.IsEnabled = false;
        ToolTip.SetTip(_framesToggle, "Draw each rigid body's measured local frame (body cinfo position and " +
                                      "orientation) as an axis triad, plus a marker at its measured world-space " +
                                      "center of mass (motionCinfos centerOfMassWorld).");
        _framesToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowFrames = _framesToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _engineerToggle.IsEnabled = false;
        ToolTip.SetTip(_engineerToggle, "Engineering view: draw only the measured body frames (axis triads + " +
                                        "center-of-mass markers) over the ground grid - no skeleton, hulls, " +
                                        "constraints or bindings - and never auto-frame the camera. A pinned " +
                                        "readout table lists every body's measured frame origin and COM so all " +
                                        "of them can be compared at once. Fit still frames the bodies.");
        _engineerToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.EngineeringFrames = _engineerToggle.IsChecked == true;
            if (_engineerToggle.IsChecked == true)
                foreach (var other in new[]
                         { _bodiesToggle, _constraintsToggle, _bindingsToggle, _physicsSkeletonToggle })
                    other.IsChecked = false;
            _skeleton.InvalidateVisual();
        };

        ToolTip.SetTip(travel, "Move the character along the path the clip carries, instead of " +
                               "playing it on the spot the way the file stores it.");
        travel.IsCheckedChanged += (_, _) =>
        {
            _followTravel = travel.IsChecked == true;
            ShowFrame(_playback.Frame, stop: false);
        };

        var reload = Ux.Secondary("From selected node");
        reload.Click += (_, _) => LoadPoseFromSelection(announce: true);

        var mesh = Ux.Secondary("Mesh...");
        mesh.Click += async (_, _) => await PickMesh();
        ToolTip.SetTip(mesh, "A .nif to draw on this skeleton");
        var clearMesh = Ux.Secondary("No mesh");
        clearMesh.Click += (_, _) => { ClearMesh(); ShowFrame(_playback.Frame, stop: false); };

        _copyDiagnosticsButton = Ux.Secondary("Copy diagnostics");
        ToolTip.SetTip(_copyDiagnosticsButton,
            "Copy why this file's physics loaded or did not: status, findings, class counts, file " +
            "version and pointer size, plus the BGS build. Paste it into a bug report.");
        _copyDiagnosticsButton.IsEnabled = false;
        _copyDiagnosticsButton.Click += async (_, _) => await CopyPhysicsDiagnostics();

        _scrub.PropertyChanged += (_, e) =>
        {

            if (e.Property != Avalonia.Controls.Primitives.RangeBase.ValueProperty || _scrubbing) return;
            ShowFrame((int)Math.Round(_scrub.Value), stop: true);
        };

        _skeleton.BoneHovered += _ => _skeleton.InvalidateVisual();
        _skeleton.ConstraintSelectionChanged += ShowConstraintInspection;
        _skeleton.FrameDistanceChanged += ShowFrameDistance;
        _clearFrameDistanceButton.IsEnabled = false;
        _clearFrameDistanceButton.Click += (_, _) => _skeleton.ClearFrameDistance();

        _scalePill = Ux.Pill(_scaleStatus);
        _scalePill.IsVisible = false;
        _frameDistancePill = Ux.Pill(_frameDistanceStatus);
        _frameDistancePill.IsVisible = false;

        var panel = new Grid();
        panel.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        panel.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        panel.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));
        panel.RowDefinitions.Add(new RowDefinition(GridLength.Auto));

        var summary = Ux.Pill(_playbackSummary);
        summary.Margin = new Thickness(0, 0, 0, Ux.Space);
        Grid.SetRow(summary, 0);
        panel.Children.Add(summary);

        RegisterToolbar("Playback",
            ToolbarGroup("Playback controls", _playButton, first, back, forward, last, fit),
            ToolbarGroup("Viewport overlays", reference, travel, _bodiesToggle, _constraintsToggle, _bindingsToggle,
                _physicsSkeletonToggle, _framesToggle, _engineerToggle),
            ToolbarGroup("Scene tools", _clearFrameDistanceButton, _dropButton, reload, mesh, clearMesh,
                _copyDiagnosticsButton));

        _skeleton.ClipToBounds = true;
        _playbackViewportHost = Framed(_skeleton);
        _playbackViewportHost.ClipToBounds = true;
        var viewport = WithClipPicker(_playbackViewportHost);
        Grid.SetRow(viewport, 2);
        panel.Children.Add(viewport);

        var timeline = Ux.Group("Timeline", _scrub,
            Ux.Wrap(Ux.Pill(_frameLabel), _scalePill, _frameDistancePill));
        timeline.Margin = new Thickness(0, Ux.Space, 0, 0);
        Grid.SetRow(timeline, 3);
        panel.Children.Add(timeline);

        SetPlaybackSummary("Open a behaviour and select a clip to see what it plays. That animates " +
                           "the skeleton; use Mesh... to hang a model on it.", Ux.MutedBrush);
        return panel;
    }

    private void TryLoadRagdoll(string path)
    {
        byte[] raw;
        try { raw = File.ReadAllBytes(path); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            ClearPhysicsReport();
            return;
        }

        var own = HavokPhysicsExtractor.TryExtract(raw, out var ownReport);
        var choice = ChooseRagdoll(path, own, ownReport, SiblingSkeletonPath(path), sibling =>
        {
            var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(sibling), out var report);
            return (model, report);
        });

        _ragdoll = choice.Model;
        _ragdollReport = choice.Report;
        _ragdollReportPath = choice.ReportPath;
        string? ragdollSource = choice.SourcePath;
        bool any = _ragdoll is { Bodies.Count: > 0 };
        _ragdollSourceName = ragdollSource == null ? null : Path.GetFileName(ragdollSource);
        _physicsReportNote = DescribePhysicsReport(path);
        if (_copyDiagnosticsButton != null) _copyDiagnosticsButton.IsEnabled = _ragdollReport != null;
        _bodiesToggle.IsEnabled = any;
        _bodiesToggle.IsChecked = any;
        _constraintsToggle.IsEnabled = any;
        _constraintsToggle.IsChecked = any && _ragdoll is { Constraints.Count: > 0 };
        bool bindings = any && _ragdoll is { BoneBindings.Count: > 0, Skeleton: { BoneNames.Count: > 0 } };
        _bindingsToggle.IsEnabled = bindings;
        _bindingsToggle.IsChecked = bindings;
        bool physicsSkeleton = any && _ragdoll is { Skeleton: { ParentIndices.Count: > 0, ReferencePose.Count: > 0 } };
        _physicsSkeletonToggle.IsEnabled = physicsSkeleton;
        _physicsSkeletonToggle.IsChecked = physicsSkeleton;
        _framesToggle.IsEnabled = any;
        _framesToggle.IsChecked = any;
        _engineerToggle.IsEnabled = any;
        _dropButton.IsEnabled = any;
        if (!any && _skeleton.IsDropped) _skeleton.ClearDrop();
        _skeleton.SetBodies(any ? _ragdoll : null);
        UpdateScaleStatus();
        if (!any)
        {
            ShowPhysicsReport(path);
            return;
        }

        bool mappings = any && _ragdoll is { Mappings.Count: > 0, AnimationSkeleton: { BoneNames.Count: > 0 } };
        string source = ragdollSource == null
            ? "measured from this file"
            : $"measured from the sibling skeleton {Path.GetFileName(ragdollSource)}";
        SetPlaybackSummary(
            $"{_ragdoll!.Bodies.Count} rigid bodies, {_ragdoll.Shapes.Count} shapes, " +
            $"{_ragdoll.Constraints.Count} constraints, {_ragdoll.BoneBindings.Count} bone bindings " +
            $"and {_ragdoll.Mappings.Count} animation-to-ragdoll bone mappings {source}. " +
            "Bodies, Constraints, Bone labels, Physics skeleton and Body frames draw them as a " +
            (mappings
                ? "file-space overlay that follows the playing animation through the measured mapper; it is not physics yet."
                : "static file-space overlay; it is not physics yet."), Ux.MetaBrush);

        _ragdollSourceNote = ragdollSource == null
            ? null
            : $"{_ragdoll.Bodies.Count} rigid bodies and {_ragdoll.Mappings.Count} bone mappings " +
              $"loaded from the sibling skeleton {_ragdollSourceName}; the ragdoll overlay follows this clip.";

        HkxSkeleton? skeleton = null;
        try { skeleton = new HkxBinaryReader().ReadSkeleton(path); }
        catch { skeleton = null; }
        if (skeleton != null && skeleton.BoneNames.Count > 0)
            _skeleton.Show(AnimationPose.ReferencePose(skeleton));
    }

    internal static RagdollChoice ChooseRagdoll(string path, HavokRagdollModel? own, PhysicsLoadReport ownReport,
                                                string? sibling,
                                                Func<string, (HavokRagdollModel? Model, PhysicsLoadReport Report)> extract)
    {
        var choice = new RagdollChoice(own, ownReport, path, null);
        if (own is { Bodies.Count: > 0 } || sibling == null) return choice;

        HavokRagdollModel? siblingModel;
        PhysicsLoadReport siblingReport;
        try { (siblingModel, siblingReport) = extract(sibling); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            var failed = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ParseStage = "sibling read",
                ParseMessage = error.Message,
            });
            return new RagdollChoice(null, failed, sibling, null);
        }

        if (siblingModel is { Bodies.Count: > 0 })
            return new RagdollChoice(siblingModel, siblingReport, sibling, sibling);

        if (siblingReport.Status != PhysicsLoadStatus.SkeletonOnly)
            return new RagdollChoice(null, siblingReport, sibling, null);

        return choice;
    }

    private void ClearPhysicsReport()
    {
        _ragdollReport = null;
        _ragdollReportPath = "";
        _physicsReportNote = null;
        if (_copyDiagnosticsButton != null) _copyDiagnosticsButton.IsEnabled = false;
    }

    private void ShowPhysicsReport(string path)
    {
        _physicsReportNote = DescribePhysicsReport(path);
        if (_physicsReportNote is not { Length: > 0 } note) return;

        SetPlaybackSummary(note, PhysicsReportBrush());
    }

    private string? DescribePhysicsReport(string path)
    {
        if (_ragdollReport is not { } report || report.Status == PhysicsLoadStatus.Ok) return null;

        bool aboutThisFile = string.Equals(SafeFullPath(path), SafeFullPath(_ragdollReportPath),
                                          StringComparison.OrdinalIgnoreCase);
        string prefix = aboutThisFile || _ragdollReportPath.Length == 0
            ? ""
            : $"The sibling skeleton {Path.GetFileName(_ragdollReportPath)}: ";
        return prefix + report.Summary;
    }

    private IBrush PhysicsReportBrush() => _ragdollReport?.Severity switch
    {
        PhysicsLoadSeverity.Info => Ux.MutedBrush,
        PhysicsLoadSeverity.Warning => Ux.WarnBrush,
        _ => Ux.BadBrush,
    };

    private async Task CopyPhysicsDiagnostics()
    {
        if (_ragdollReport is not { } report)
        {
            SetStatus("There is no physics report to copy yet; open a file first.", Ux.MutedBrush);
            return;
        }

        long? size = null;
        try { if (File.Exists(_ragdollReportPath)) size = new FileInfo(_ragdollReportPath).Length; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }

        string text = report.Diagnostics(_ragdollReportPath, size, BuildInfo.Report);
        var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
        if (clipboard == null)
        {
            SetStatus("This environment has no clipboard, so the physics diagnostics were not copied.", Ux.WarnBrush);
            return;
        }

        try
        {
            await clipboard.SetTextAsync(text);
            SetStatus($"Physics diagnostics for {Path.GetFileName(_ragdollReportPath)} copied " +
                      $"({report.StatusLabel}).", Ux.MetaBrush);
        }
        catch (Exception error)
        {
            SetStatus("The physics diagnostics could not be copied: " + error.Message, Ux.WarnBrush);
        }
    }

    private static string SafeFullPath(string path)
    {
        try { return path.Length == 0 ? "" : Path.GetFullPath(path); }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return path;
        }
    }

    private HkxSkeleton? PoseSkeleton(string? animationPath = null)
    {
        if (_cachedSkeletonFor == _hkxPath && _cachedSkeleton != null) return _cachedSkeleton;

        _cachedSkeletonFor = _hkxPath;
        _cachedSkeleton = _projectChain?.Skeleton ?? SiblingSkeleton(_hkxPath, animationPath);
        return _cachedSkeleton;
    }

    private void LoadPoseFromSelection(bool announce)
    {
        if (_xmlText.Length == 0 || _selectedId.Length == 0)
        {
            if (announce) SetPlaybackSummary("Select a clip generator in the graph or the tree first.", Ux.MutedBrush);
            return;
        }

        string animation = "";
        foreach (var p in HkxTextEdit.ReadParams(_xmlText, _selectedId))
            if (p.Name == "animationName") animation = p.Value.Trim();

        if (animation.Length == 0)
        {
            if (announce)
                SetPlaybackSummary($"{Describe(_selectedId)} names no animation, so there is nothing to play.",
                                   Ux.MutedBrush);
            return;
        }

        string root = _projectChain?.Root ?? Path.GetDirectoryName(Path.GetFullPath(_hkxPath)) ?? "";
        string path = ProjectChain.ResolvePath(root, animation);
        if (File.Exists(path))
        {
            LoadPose(path, animation);
            return;
        }

        GameData.AnimationRead? packed;
        try { packed = _gameData?.ReadAnimation(root, animation); }
        catch (Exception e) when (e is IOException or InvalidDataException)
        {
            SetPlaybackSummary($"'{animation}' could not be read: {e.Message.Split('\n')[0]}", Ux.BadBrush);
            return;
        }
        if (packed == null)
        {
            if (announce)
                SetPlaybackSummary($"'{animation}' is neither loose under {root} nor inside any .ba2 under " +
                                   "the game data folder, so it cannot be played. Check graph reports " +
                                   "the same thing.", Ux.BadBrush);
            return;
        }

        LoadPose(packed.Bytes, animation, animation, packed.Source, packed.EntryName);
    }

    private void LoadPose(string animationPath, string label)
    {
        byte[] hkx;
        try { hkx = InputFilePolicy.ReadHkx(animationPath); }
        catch (Exception ex)
        {
            ClearPose();
            SetPlaybackSummary($"Could not read {label}: {ex.Message.Split('\n')[0]}", Ux.BadBrush);
            return;
        }
        LoadPose(hkx, animationPath, label, "loose", null);
    }

    private void LoadPose(byte[] hkx, string key, string label, string source, string? archiveEntry)
    {
        if (_poseSource == key) return;

        Stop();
        bool foreign = !string.Equals(SafeFullPath(_hkxPath), SafeFullPath(key), StringComparison.OrdinalIgnoreCase);
        if (foreign) ClearPhysicsReport();
        _poseSkeleton = PoseSkeleton(archiveEntry == null ? key : null) ?? ArchiveSkeleton(archiveEntry);

        HkxAnimationData animation;
        try
        {
            if (!new HkxBinaryReader().TryReadAnimation(hkx, out animation))
            {
                SetPlaybackSummary($"{label}: {AnimationCompression.Describe(animation.AnimationClass)}",
                                   Ux.BadBrush);
                ClearPose();
                SetPlaybackSummary(
                    $"{label}: {animation.UnsupportedReason} in {animation.AnimationClass}, so it cannot be drawn.",
                    Ux.BadBrush);
                return;
            }
        }
        catch (Exception ex)
        {
            ClearPose();
            SetPlaybackSummary($"Could not read {label}: {ex.Message.Split('\n')[0]}", Ux.BadBrush);
            return;
        }

        string? refusal = AnimationPose.WhyNotPosable(_poseSkeleton, animation);
        if (refusal != null)
        {
            SetPlaybackSummary($"{label}: {refusal}", Ux.WarnBrush);

            if (_poseSkeleton != null) _skeleton.Show(AnimationPose.ReferencePose(_poseSkeleton));
            _poseAnimation = null;
            _poseSource = "";
            _playback.Clear();
            _scrub.Maximum = 0;
            AppendPhysicsNotes();
            return;
        }

        _poseAnimation = animation;
        _poseSource = key;
        _playback.Load(animation.NumFrames, animation.FrameDuration);

        try { _poseMotion = RootMotion.Read(hkx); }
        catch { _poseMotion = new RootMotion.Motion(); }

        var reference = AnimationPose.ReferencePose(_poseSkeleton!);
        var opening = AnimationPose.At(_poseSkeleton!, animation, 0);
        _skeleton.Show(opening, reference);
        UpdateMesh(opening, _poseSkeleton!);

        _scrubbing = true;
        _scrub.Maximum = Math.Max(0, animation.NumFrames - 1);
        _scrub.Value = 0;
        _scrubbing = false;

        int driven = 0;
        foreach (int track in AnimationPose.TracksByBone(_poseSkeleton!, animation)) if (track >= 0) driven++;

        string travelled = _poseMotion.Any
            ? $"   travels {_poseMotion.Travel.Length():F0} units" +
              (Math.Abs(_poseMotion.Turn) > 0.02f
                  ? $" and turns {_poseMotion.Turn * 180 / MathF.PI:F0} degrees"
                  : "")
            : "   stays on the spot";

        string from = source == "loose" ? "" : $"   read from {source}";
        SetPlaybackSummary(
            $"{label}   {animation.NumFrames} frames at {1f / Math.Max(animation.FrameDuration, 0.0001f):F0} fps, " +
            $"{animation.Duration:F2}s   {driven} of {_poseSkeleton!.BoneNames.Count} bones driven   " +
            $"on {_poseSkeleton.Name}{travelled}{from}", Ux.MetaBrush);
        UpdateFrameLabel();
        AppendPhysicsNotes();
    }

    private HkxSkeleton? ArchiveSkeleton(string? animationEntry)
    {
        if (_gameData == null || animationEntry == null) return null;
        var gameData = _gameData;
        byte[]? bytes = SkeletonBytesOrNull(() => gameData.SkeletonBytes(animationEntry));
        if (bytes == null) return null;
        try { return new HkxBinaryReader().ReadSkeleton(bytes); }
        catch { return null; }
    }

    internal static byte[]? SkeletonBytesOrNull(Func<byte[]?> read)
    {
        try { return read(); }
        catch (Exception e) when (GameData.IsSkippableArchiveFailure(e)) { return null; }
    }

}
