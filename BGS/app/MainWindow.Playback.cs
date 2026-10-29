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

        _referenceToggle.IsCheckedChanged += (_, _) =>
        {
            _skeleton.ShowReference = _referenceToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _bodiesToggle.IsEnabled = false;
        ToolTip.SetTip(_bodiesToggle, "Draw the rigid bodies this file measures (hknpRagdollData), " +
                                      "as a static file-space overlay on the skeleton.");
        _bodiesToggle.IsCheckedChanged += (_, _) =>
        {
            MarkOverlayChoice(_bodiesToggle);
            _skeleton.ShowBodies = _bodiesToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _constraintsToggle.IsEnabled = false;
        ToolTip.SetTip(_constraintsToggle, "Draw this file's measured ragdoll constraints: body links, " +
                                          "pivots (transformA/transformB) and the twist/cone/angle limits " +
                                          "the constraint atoms store.");
        _constraintsToggle.IsCheckedChanged += (_, _) =>
        {
            MarkOverlayChoice(_constraintsToggle);
            _skeleton.ShowConstraints = _constraintsToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _bindingsToggle.IsEnabled = false;
        ToolTip.SetTip(_bindingsToggle, "Label each bound rigid body with its bone name from the physics " +
                                        "skeleton (hknpRagdollData.boneToBodyMap). Hover a body to highlight " +
                                        "the body-to-bone association.");
        _bindingsToggle.IsCheckedChanged += (_, _) =>
        {
            MarkOverlayChoice(_bindingsToggle);
            _skeleton.ShowBindings = _bindingsToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _physicsSkeletonToggle.IsEnabled = false;
        ToolTip.SetTip(_physicsSkeletonToggle, "Draw the physics skeleton hknpRagdollData references (hkaSkeleton): " +
                                               "its bone chain and reference pose, composed down the parent " +
                                               "indices into file space.");
        _physicsSkeletonToggle.IsCheckedChanged += (_, _) =>
        {
            MarkOverlayChoice(_physicsSkeletonToggle);
            _skeleton.ShowPhysicsSkeleton = _physicsSkeletonToggle.IsChecked == true;
            _skeleton.InvalidateVisual();
        };

        _framesToggle.IsEnabled = false;
        ToolTip.SetTip(_framesToggle, "Draw each rigid body's measured local frame (body cinfo position and " +
                                      "orientation) as an axis triad, plus a marker at its measured world-space " +
                                      "center of mass (motionCinfos centerOfMassWorld).");
        _framesToggle.IsCheckedChanged += (_, _) =>
        {
            MarkOverlayChoice(_framesToggle);
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
            {
                _overlayDefaultsApplying = true;
                try
                {
                    foreach (var other in new[]
                             { _bodiesToggle, _constraintsToggle, _bindingsToggle, _physicsSkeletonToggle })
                        other.IsChecked = false;
                }
                finally
                {
                    _overlayDefaultsApplying = false;
                }
            }
            _skeleton.InvalidateVisual();
        };

        ToolTip.SetTip(_travelToggle, "Move the character along the path the clip carries, instead of " +
                                      "playing it on the spot the way the file stores it.");
        _travelToggle.IsCheckedChanged += (_, _) =>
        {
            _followTravel = _travelToggle.IsChecked == true;
            ShowFrame(_playback.Frame, stop: false);
        };

        _reloadFromNodeButton.Click += (_, _) => LoadPoseFromSelection(announce: true);

        _meshButton.Click += async (_, _) => await PickMesh();
        ToolTip.SetTip(_meshButton, "A .nif to draw on this skeleton");
        _noMeshButton.Click += (_, _) => { ClearMesh(); ShowFrame(_playback.Frame, stop: false); };

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

        RegisterToolbar("Playback", PlaybackToolRibbon());

        var panel = new Grid();
        panel.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        panel.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));

        var summary = Ux.Pill(_playbackSummary);
        summary.Margin = new Thickness(0, 0, 0, Ux.Space);
        Grid.SetRow(summary, 0);
        panel.Children.Add(summary);

        _skeleton.ClipToBounds = true;
        _playbackViewportHost = Framed(ViewportLayers());
        _playbackViewportHost.ClipToBounds = true;
        var viewport = WithClipPicker(_playbackViewportHost);
        Grid.SetRow(viewport, 1);
        panel.Children.Add(viewport);
        SyncViewport();

        SetPlaybackSummary("Open a behaviour and select a clip to see what it plays. That animates " +
                           "the skeleton; use Mesh... to hang a model on it.", Ux.MutedBrush);
        UpdateTransportState();
        return panel;
    }

    private void ApplyOverlayDefault(CheckBox toggle, bool wanted)
    {
        if (_overlayChosenByUser.Contains(toggle)) return;

        _overlayDefaultsApplying = true;
        try
        {
            toggle.IsChecked = wanted;
        }
        finally
        {
            _overlayDefaultsApplying = false;
        }
    }

    private void MarkOverlayChoice(CheckBox toggle)
    {
        if (!_overlayDefaultsApplying) _overlayChosenByUser.Add(toggle);
    }

    private void QuietOverlaysForMesh()
    {
        if (_ragdoll is not { Bodies.Count: > 0 }) return;

        ApplyOverlayDefault(_bodiesToggle, false);
        ApplyOverlayDefault(_bindingsToggle, false);
        ApplyOverlayDefault(_physicsSkeletonToggle, false);
        ApplyOverlayDefault(_framesToggle, false);
    }

    private void TryLoadRagdoll(string path, string? virtualPathHint = null)
    {
        byte[] raw;
        try { raw = File.ReadAllBytes(path); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            ClearPhysicsReport();
            return;
        }

        var own = HavokPhysicsExtractor.TryExtract(raw, out var ownReport);
        string? sibling = SiblingSkeletonPath(path);
        string? packedAnimation = null;
        if (own is not { Bodies.Count: > 0 } && sibling == null && _gameData != null)
        {
            packedAnimation = PackedAnimationKey(_gameData, path, virtualPathHint);
            if (packedAnimation != null) sibling = _gameData.SkeletonEntryName(packedAnimation);
        }

        _ragdollSiblingName = sibling == null ? null : Path.GetFileName(sibling);

        var choice = ChooseRagdoll(path, own, ownReport, sibling, key =>
        {
            byte[] bytes = PackedSkeleton(_gameData, packedAnimation, key);
            var model = HavokPhysicsExtractor.TryExtract(bytes, out var report);
            return (model, report);
        });

        _ragdoll = choice.Model;
        _ragdollReport = choice.Report;
        _ragdollReportPath = choice.ReportPath;
        string? ragdollSource = choice.SourcePath;
        bool any = _ragdoll is { Bodies.Count: > 0 };
        _ragdollSourceName = ragdollSource == null ? null : Path.GetFileName(ragdollSource);
        _physicsReportNote = DescribePhysicsReport(path);
        _copyDiagnosticsButton.IsEnabled = _ragdollReport != null;
        bool quiet = any && _meshShapes.Count > 0;
        _bodiesToggle.IsEnabled = any;
        ApplyOverlayDefault(_bodiesToggle, any && !quiet);
        _constraintsToggle.IsEnabled = any;
        ApplyOverlayDefault(_constraintsToggle, any && _ragdoll is { Constraints.Count: > 0 });
        bool bindings = any && _ragdoll is { BoneBindings.Count: > 0, Skeleton: { BoneNames.Count: > 0 } };
        _bindingsToggle.IsEnabled = bindings;
        ApplyOverlayDefault(_bindingsToggle, bindings && !quiet);
        bool physicsSkeleton = any && _ragdoll is { Skeleton: { ParentIndices.Count: > 0, ReferencePose.Count: > 0 } };
        _physicsSkeletonToggle.IsEnabled = physicsSkeleton;
        ApplyOverlayDefault(_physicsSkeletonToggle, physicsSkeleton && !quiet);
        _framesToggle.IsEnabled = any;
        ApplyOverlayDefault(_framesToggle, any && !quiet);
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

        RefreshDiagnosticsPanel();

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

    internal static string? PackedAnimationKey(GameData? data, string path, string? virtualPathHint)
    {
        if (!string.IsNullOrWhiteSpace(virtualPathHint) && AnimationIndex.IsSafeVirtualPath(virtualPathHint))
            return virtualPathHint.Replace('\\', '/').TrimStart('/');
        return data?.DataRelativePath(path);
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

    private void NoteAnimationRagdoll()
    {
        if (_ragdollReport is not { Status: PhysicsLoadStatus.SkeletonOnly }) return;
        if (_ragdollReport.ClassCounts.ContainsKey(PhysicsLoadReport.SkeletonClass)) return;

        _physicsReportNote = _ragdollSiblingName == null
            ? "This animation file holds no ragdoll of its own, and no skeleton sibling supplied one; " +
              "Fallout 4 keeps a ragdoll as hknpRagdollData inside a skeleton file."
            : $"This animation file holds no ragdoll of its own; its skeleton sibling {_ragdollSiblingName} " +
              "was found but carries none. Fallout 4 keeps a ragdoll as hknpRagdollData inside a skeleton file.";
        RefreshDiagnosticsPanel();
    }

    private void ClearPhysicsReport()
    {
        _ragdollReport = null;
        _ragdollReportPath = "";
        _physicsReportNote = null;
        _copyDiagnosticsButton.IsEnabled = false;
        RefreshDiagnosticsPanel();
    }

    private void ShowPhysicsReport(string path)
    {
        _physicsReportNote = DescribePhysicsReport(path);
        RefreshDiagnosticsPanel();
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

    private HkxSkeleton? PoseSkeleton(string? animationPath = null, string? archiveEntry = null,
                                      string? dataFolderHint = null)
    {
        if (_cachedSkeletonFor == _hkxPath && _cachedSkeleton != null) return _cachedSkeleton;

        _cachedSkeletonFor = _hkxPath;
        _cachedSkeleton = ResolveAnimationSkeleton(animationPath ?? _hkxPath, null, dataFolderHint, archiveEntry);
        return _cachedSkeleton;
    }

    private readonly List<GameData> _scopedGameData = new();

    internal static int DataFolderDiscoveriesForTest;

    internal HkxSkeleton? ResolveAnimationSkeleton(string animationPath, string? virtualPathHint,
                                                   string? dataFolderHint, string? archiveEntry = null)
    {
        if (_projectChain?.Skeleton is { } fromChain) return fromChain;

        bool packedFirst = PackedSiblingFirst(virtualPathHint, animationPath);
        if (!packedFirst && File.Exists(animationPath) && SiblingSkeleton(animationPath) is { } loose)
            return loose;

        string? entry = archiveEntry ?? PackedAnimationKey(_gameData, animationPath, virtualPathHint);
        HkxSkeleton? packed = _gameData != null
            ? ArchiveSkeleton(_gameData, entry)
            : PackedSkeletonFromDataFolders(dataFolderHint, entry);
        if (packed != null) return packed;

        if (packedFirst && File.Exists(animationPath) && SiblingSkeleton(animationPath) is { } nearby)
            return nearby;
        return null;
    }

    private static bool PackedSiblingFirst(string? virtualPathHint, string animationPath)
    {
        if (!string.IsNullOrWhiteSpace(virtualPathHint)) return true;

        string root = Path.Combine(Path.GetTempPath(), "BehaviourGraphStudio") + Path.DirectorySeparatorChar;
        return SafeFullPath(animationPath).StartsWith(root, StringComparison.OrdinalIgnoreCase);
    }

    private HkxSkeleton? PackedSkeletonFromDataFolders(string? dataFolderHint, string? entry)
    {
        foreach (GameData data in DataFolderCandidates(dataFolderHint))
        {
            HkxSkeleton? skeleton = ArchiveSkeleton(data, entry);
            if (skeleton != null) return skeleton;
        }
        return null;
    }

    private List<GameData> DataFolderCandidates(string? dataFolderHint)
    {
        if (_scopedGameData.Count > 0) return _scopedGameData;

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string folder in new[] { dataFolderHint ?? "", Settings.Get("gameDataFolder") })
        {
            if (folder.Length == 0 || !Directory.Exists(folder)) continue;
            string full = SafeFullPath(folder);
            if (full.Length == 0 || !seen.Add(full)) continue;
            try
            {
                GameData discovered = GameData.Discover(full);
                DataFolderDiscoveriesForTest++;
                _scopedGameData.Add(discovered);
            }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { }
        }
        return _scopedGameData;
    }

    private void ReleaseScopedGameData()
    {
        foreach (GameData data in _scopedGameData) data.Dispose();
        _scopedGameData.Clear();
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

    private void LoadPose(string animationPath, string label, string? virtualPathHint = null,
                          string? dataFolderHint = null)
    {
        byte[] hkx;
        try { hkx = InputFilePolicy.ReadHkx(animationPath); }
        catch (Exception ex)
        {
            ClearPose();
            SetPlaybackSummary($"Could not read {label}: {ex.Message.Split('\n')[0]}", Ux.BadBrush);
            return;
        }
        LoadPose(hkx, animationPath, label, "loose",
                 PackedAnimationKey(_gameData, animationPath, virtualPathHint), dataFolderHint ?? "");
    }

    private void LoadPose(byte[] hkx, string key, string label, string source, string? archiveEntry,
                          string dataFolderHint = "")
    {
        if (_poseSource == key) return;

        try { LoadPoseBody(hkx, key, label, source, archiveEntry, dataFolderHint); }
        finally { ReleaseScopedGameData(); }
    }

    private void LoadPoseBody(byte[] hkx, string key, string label, string source, string? archiveEntry,
                              string dataFolderHint)
    {
        Stop();
        bool foreign = !string.Equals(SafeFullPath(_hkxPath), SafeFullPath(key), StringComparison.OrdinalIgnoreCase);
        if (foreign) ClearPhysicsReport();
        try { _poseSkeleton = PoseSkeleton(key, archiveEntry, dataFolderHint); }
        catch (InvalidDataException ex)
        {
            ClearPose();
            SetPlaybackSummary($"Could not read {label}: {ex.Message.Split('\n')[0]}", Ux.BadBrush);
            return;
        }

        HkxAnimationData animation;
        try
        {
            if (!new HkxBinaryReader().TryReadAnimation(hkx, out animation))
            {
                ClearPose();
                SetPlaybackSummary($"{label}: {AnimationCompression.Describe(animation.AnimationClass)}",
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
            _poseRefusal = $"{label}: {refusal}";
            _playback.Clear();
            _scrub.Maximum = 0;
            AppendPhysicsNotes();
            UpdateTransportState();
            return;
        }

        _poseAnimation = animation;
        _poseSource = key;
        _poseRefusal = "";
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
        UpdateTransportState();
    }

    private static HkxSkeleton? ArchiveSkeleton(GameData? data, string? animationEntry)
    {
        if (data == null || animationEntry == null) return null;
        byte[]? bytes = SkeletonBytesOrNull(() => data.SkeletonBytes(animationEntry));
        if (bytes == null) return null;
        try { return new HkxBinaryReader().ReadSkeleton(bytes); }
        catch { return null; }
    }

    internal static byte[] PackedSkeleton(GameData? data, string? packedAnimation, string key)
    {
        if (packedAnimation != null && data != null &&
            string.Equals(data.SkeletonEntryName(packedAnimation), key, StringComparison.OrdinalIgnoreCase) &&
            data.TryReadSkeletonEntry(key, out byte[] packed))
        {
            return packed;
        }

        return File.ReadAllBytes(key);
    }

    internal static byte[]? SkeletonBytesOrNull(Func<byte[]?> read)
    {
        try { return read(); }
        catch (Exception e) when (GameData.IsSkippableArchiveFailure(e)) { return null; }
    }

}
