using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

public sealed partial class RigAuthoringWindow
{
    private HavokRagdollModel? _physics;
    private PhysicsLoadReport? _physicsReport;
    private string _physicsReportPath = "";
    private RagdollDocument? _ragdoll;
    private string _ragdollPath = "";
    private RagdollSimulation? _simulation;
    private readonly Dictionary<int, float> _masses = new();
    private readonly Dictionary<int, HavokConstraint> _previewJoints = new();
    private readonly SkeletonView _physicsView = new() { ShowBodies = true, ShowConstraints = true };
    private readonly ComboBox _physicsBodies = new() { MinWidth = 220 };
    private readonly ComboBox _physicsJoints = new() { MinWidth = 220 };
    private readonly TextBox _mass = Ux.Field("positive mass", 110);
    private readonly TextBox _gravity = Ux.Field("gravity", 100);
    private readonly TextBox _ground = Ux.Field("ground Z", 100);
    private readonly TextBox _impulse = Ux.Field("X Y Z impulse", 180);
    private readonly ComboBox _physicsRagdolls = new() { MinWidth = 200 };
    private readonly ComboBox _physicsMaterials = new() { MinWidth = 120 };
    private readonly TextBox _materialDynamic = Ux.Field("dynamic friction", 110);
    private readonly TextBox _materialStatic = Ux.Field("static friction", 110);
    private readonly CheckBox _materialShared = new() { Content = "Allow shared material" };
    private readonly ComboBox _jointAxis = new() { ItemsSource = new[] { "X", "Y", "Z" }, SelectedIndex = 0, Width = 70 };
    private readonly ComboBox _jointRef = new() { ItemsSource = new[] { "X", "Y", "Z" }, SelectedIndex = 1, Width = 70 };
    private readonly TextBox _angleMin = Ux.Field("min radians", 100);
    private readonly TextBox _angleMax = Ux.Field("max radians", 100);
    private readonly TextBox _cone = Ux.Field("cone radians", 100);
    private readonly DispatcherTimer _physicsTimer = new() { Interval = TimeSpan.FromSeconds(1d / 60) };
    private readonly Stopwatch _physicsClock = new();
    private double _lastTick;

    private Control PhysicsPanel()
    {
        _mass.Name = "SimulationMass";
        _gravity.Name = "SimulationGravity";
        _ground.Name = "SimulationGround";
        _gravity.Text = "600";
        _ground.Text = "0";
        _impulse.Text = "0 0 100";
        _physicsRagdolls.Name = "RagdollSelection";
        _physicsMaterials.Name = "MaterialSelection";
        _materialDynamic.Name = "MaterialDynamicFriction";
        _materialStatic.Name = "MaterialStaticFriction";
        _materialShared.Name = "MaterialAllowShared";
        _angleMin.Text = "-0.5";
        _angleMax.Text = "0.5";
        _cone.Text = "0.8";
        var panel = new StackPanel { Spacing = 10 };
        panel.Children.Add(Row(Button("Open ragdoll HKX", () => _ = BrowsePhysics()),
            Button("Save ragdoll values", () => Run(SaveRagdollValues)),
            Button("Revert ragdoll values", () => Run(RevertRagdollValues)),
            Button("Start / resume simulation", () => Run(StartPhysics)),
            Button("Pause simulation", PausePhysics), Button("Reset simulation", ResetPhysics),
            Button("Step physics", () => Run(() => { EnsureSimulation(); PausePhysics(); AdvancePhysics(1f / 60); })),
            Button("Copy diagnostics", () => _ = CopyPhysicsDiagnostics())));
        panel.Children.Add(new TextBlock { Text = "Interactive BEPU physics with gravity, hull collisions and preview joints. Simulation settings stay in this window; the file is unchanged until Save ragdoll values. This does not reproduce the game's Havok solver.", TextWrapping = TextWrapping.Wrap });
        panel.Children.Add(Row(Ux.Label("Body"), _physicsBodies, Ux.Label("Simulation mass"), _mass,
            Button("Set body mass", () => Run(() => SetMass(false))), Button("Set all masses", () => Run(() => SetMass(true)))));
        panel.Children.Add(new TextBlock { Text = "Inertia is calculated from each measured collision hull assuming uniform density. Directly connected bodies do not collide; other bodies and the ground do. Saving writes the mass as the file's inverse mass and scales the body's stored inverse inertia by the mass ratio.", TextWrapping = TextWrapping.Wrap });
        panel.Children.Add(Row(Ux.Label("Ragdoll"), _physicsRagdolls, Ux.Label("Material"), _physicsMaterials,
            Ux.Label("Dynamic"), _materialDynamic, Ux.Label("Static"), _materialStatic, _materialShared,
            Button("Set material friction", () => Run(SetMaterialValues))));
        panel.Children.Add(new TextBlock { Text = "Material friction writes the dynamic and static values separately at the file's half precision and writes only the field that changed. A material that more than one body names, or that the file flags shared, is refused unless Allow shared material is ticked. Capsule radius and endpoint edits are not offered from this tab, but symrm ragdoll --set capsule: and --set radius: resize a capsule while its direction stays the same, re-deriving the file's vertices, planes and mass properties; a capsule shape that more than one body names, or whose mass properties another capsule shape shares, needs --allow-shared-shape there.", TextWrapping = TextWrapping.Wrap });
        panel.Children.Add(Row(Ux.Label("Joint"), _physicsJoints, Ux.Label("Axis"), _jointAxis, Ux.Label("Reference"), _jointRef));
        panel.Children.Add(Row(Ux.Label("Preview limits (radians)"), _angleMin, _angleMax, Ux.Label("Cone"), _cone,
            Button("Set preview joint", () => Run(() => SetJoints(false))), Button("Set all preview joints", () => Run(() => SetJoints(true)))));
        panel.Children.Add(new TextBlock { Text = "Choose preview joint limits explicitly. Source joint frames are retained; source angular atoms, motors and mass settings are not imported. Saving writes the twist limit of a ragdoll constraint or the angular limit of a limited hinge, and the cone limit of a ragdoll constraint whose cone angle measurement mode is the verified one; plane limits are written only by the command line, and the axes, motors and frames are never written.", TextWrapping = TextWrapping.Wrap });
        panel.Children.Add(Row(Ux.Label("Gravity (file units/s²; reset to edit)"), _gravity, Ux.Label("Ground Z"), _ground,
            Ux.Label("Impulse"), _impulse, Button("Push selected body", () => Run(() => {
                EnsureSimulation(); float[] impulse = Values(_impulse, 3);
                _simulation!.Impulse(SelectedBody(), new Vector3(impulse[0], impulse[1], impulse[2]));
            }))));
        _physicsTimer.Tick += (_, _) => {
            try
            {
                double now = _physicsClock.Elapsed.TotalSeconds;
                AdvancePhysics((float)(now - _lastTick));
                _lastTick = now;
            }
            catch (Exception error) { StopPhysics(); Say("Simulation stopped: " + error.Message); }
        };
        _physicsBodies.SelectionChanged += (_, _) => {
            if (_physics == null || _physicsBodies.SelectedIndex < 0) return;
            _mass.Text = _masses.TryGetValue(SelectedBody(), out float mass) ? Text(mass) : "";
        };
        _physicsRagdolls.SelectionChanged += (_, _) => RefreshMaterialChoices();
        _physicsMaterials.SelectionChanged += (_, _) => LoadMaterialValues();
        return AuthoringPanel(panel, _physicsView);
    }

    public bool OpenPhysics(string path, out PhysicsLoadReport report)
    {
        if (_ragdoll?.Dirty == true)
            throw new InvalidOperationException(
                "Save or revert the ragdoll values before opening another ragdoll.");

        byte[] bytes;
        try { bytes = InputFilePolicy.ReadHkx(path); }
        catch (Exception error)
        {
            report = PhysicsLoadClassifier.Classify(new PhysicsLoadFacts
            {
                ParseStage = "read",
                ParseMessage = error.Message,
            });
            _physicsReport = report;
            _physicsReportPath = path;
            Say(RefusedPhysics(path, report));
            return false;
        }

        var model = HavokPhysicsExtractor.TryExtract(bytes, out report);
        _physicsReport = report;
        _physicsReportPath = path;

        if (model == null || model.Bodies.Count == 0)
        {
            Say(RefusedPhysics(path, report));
            return false;
        }

        StopPhysics();
        _physics = model;
        _ragdoll = TryOpenRagdoll(path, out string? refusal);
        _ragdollPath = _ragdoll == null ? "" : path;
        _physicsView.ShowMesh(null);
        RefreshRagdollChoices();
        _masses.Clear(); _previewJoints.Clear();
        _physicsBodies.ItemsSource = model.Bodies.Select(body => $"{body.Id}: {body.Name}").ToArray();
        _physicsBodies.SelectedIndex = 0;
        _mass.Text = "";
        _physicsJoints.ItemsSource = model.Constraints.Select(joint => $"{joint.Id}: {joint.Kind}").ToArray();
        _physicsJoints.SelectedIndex = model.Constraints.Count > 0 ? 0 : -1;
        _physicsView.SetBodies(model);
        _physicsView.Frame();
        Say($"Loaded {model.Bodies.Count} bodies and {model.Constraints.Count} joints. " +
            $"Set simulation masses and preview limits before starting. ({report.StatusLabel})" +
            (_ragdoll == null ? " " + refusal : " Masses and limits save into this file; nothing is written until Save ragdoll values."));
        return true;
    }

    private static RagdollDocument? TryOpenRagdoll(string path, out string? refusal)
    {
        try
        {
            refusal = null;
            return RagdollDocument.Open(path);
        }
        catch (Exception error)
        {
            refusal = "The ragdoll's values are preview-only here: " + error.Message;
            return null;
        }
    }

    private string RefusedPhysics(string path, PhysicsLoadReport report) =>
        $"No ragdoll loaded from {Path.GetFileName(path)} ({report.StatusLabel}). {report.Summary}" +
        (_physics is { Bodies.Count: > 0 } ? " The previous ragdoll kept in the preview is unchanged." : "");

    public string PhysicsStatusForTest => _status.Text ?? "";

    public RagdollDocument? RagdollForTest => _ragdoll;

    private void SaveRagdollValues()
    {
        var document = _ragdoll
            ?? throw new InvalidOperationException(
                "This ragdoll's values are preview-only: the file holds no hknpRagdollData this build can edit.");
        if (!document.Dirty)
        {
            Say("No ragdoll values to save: nothing was edited.");
            return;
        }

        document.Save();
        Say($"Ragdoll values saved to {Path.GetFileName(_ragdollPath)}; original retained as .bak.");
    }

    private void RevertRagdollValues()
    {
        var document = _ragdoll
            ?? throw new InvalidOperationException(
                "This ragdoll's values are preview-only: the file holds no hknpRagdollData this build can edit.");
        document.Reload();
        _masses.Clear();
        _previewJoints.Clear();
        RebuildPhysicsFromDocument(document);
        RefreshMaterialChoices();
        Say("Ragdoll values reverted to the bytes this document was read from; simulation masses and preview " +
            "joints were cleared and the physics preview was rebuilt from them.");
    }

    private void RebuildPhysicsFromDocument(RagdollDocument document)
    {
        StopPhysics();
        var model = HavokPhysicsExtractor.TryExtract(document.SourceBytes, out PhysicsLoadReport report);
        _physicsReport = report;
        if (model == null || model.Bodies.Count == 0)
        {
            _physics = null;
            Say("The physics preview could not be rebuilt from the reverted bytes: " + report.Summary);
            return;
        }

        _physics = model;
        _physicsView.ShowMesh(null);
        _physicsBodies.ItemsSource = model.Bodies.Select(body => $"{body.Id}: {body.Name}").ToArray();
        _physicsBodies.SelectedIndex = 0;
        _mass.Text = "";
        _physicsJoints.ItemsSource = model.Constraints.Select(joint => $"{joint.Id}: {joint.Kind}").ToArray();
        _physicsJoints.SelectedIndex = model.Constraints.Count > 0 ? 0 : -1;
        _physicsView.SetBodies(model);
        _physicsView.Frame();
    }

    private void RefreshRagdollChoices()
    {
        _physicsRagdolls.ItemsSource = _ragdoll == null
            ? Array.Empty<string>()
            : _ragdoll.Ragdolls.Select((node, index) => $"{index}: ragdoll #{node.ObjectId}").ToArray();
        _physicsRagdolls.SelectedIndex = _ragdoll is { Ragdolls.Count: > 0 } ? 0 : -1;
        RefreshMaterialChoices();
    }

    private void RefreshMaterialChoices()
    {
        var node = CurrentRagdoll();
        _physicsMaterials.ItemsSource = node == null
            ? Array.Empty<string>()
            : node.Materials.Select(material => $"{material.Index}").ToArray();
        _physicsMaterials.SelectedIndex = node is { Materials.Count: > 0 } ? 0 : -1;
        LoadMaterialValues();
    }

    private RagdollNode? CurrentRagdoll() =>
        _ragdoll is { Ragdolls.Count: > 0 } document && _physicsRagdolls.SelectedIndex >= 0
            ? document.Ragdolls[Math.Min(_physicsRagdolls.SelectedIndex, document.Ragdolls.Count - 1)]
            : null;

    private int SelectedRagdoll() => CurrentRagdoll() == null
        ? 0
        : Math.Min(_physicsRagdolls.SelectedIndex, _ragdoll!.Ragdolls.Count - 1);

    private void LoadMaterialValues()
    {
        var node = CurrentRagdoll();
        if (node == null || _physicsMaterials.SelectedIndex < 0 ||
            _physicsMaterials.SelectedIndex >= node.Materials.Count)
            return;

        var material = node.Materials[_physicsMaterials.SelectedIndex];
        _materialDynamic.Text = Text(material.DynamicFriction);
        _materialStatic.Text = Text(material.StaticFriction);
        _materialShared.IsChecked = material.FileSaysShared;
    }

    private void SetMaterialValues()
    {
        var document = _ragdoll;
        if (document is not { Ragdolls.Count: > 0 })
            throw new InvalidOperationException(
                "This ragdoll's values are preview-only: the file holds no hknpRagdollData this build can edit.");
        if (_physicsMaterials.SelectedIndex < 0) throw new InvalidOperationException("Choose a material.");

        int ragdoll = SelectedRagdoll();
        int material = _physicsMaterials.SelectedIndex;
        float dynamic = Values(_materialDynamic, 1)[0];
        float rest = Values(_materialStatic, 1)[0];
        document.SetMaterialFriction(ragdoll, material, dynamic, rest, _materialShared.IsChecked == true);

        var stored = document.Ragdolls[ragdoll].Materials[material];
        Say($"Material {material} set to dynamic {Text(stored.DynamicFriction)} and static " +
            $"{Text(stored.StaticFriction)} at the file's half precision; save ragdoll values to write it.");
    }

    private async Task CopyPhysicsDiagnostics()
    {
        if (_physicsReport is not { } report)
        {
            Say("There is no physics report to copy yet; open a ragdoll HKX first.");
            return;
        }

        long? size = null;
        try { if (File.Exists(_physicsReportPath)) size = new FileInfo(_physicsReportPath).Length; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }

        string text = report.Diagnostics(_physicsReportPath, size, BuildInfo.Report);
        var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
        if (clipboard == null)
        {
            Say("This environment has no clipboard, so the physics diagnostics were not copied.");
            return;
        }

        try
        {
            await clipboard.SetTextAsync(text);
            Say($"Physics diagnostics for {Path.GetFileName(_physicsReportPath)} copied ({report.StatusLabel}).");
        }
        catch (Exception error)
        {
            Say("The physics diagnostics could not be copied: " + error.Message);
        }
    }

    private async Task BrowsePhysics()
    {
        try
        {
            var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions { Title = "Open ragdoll HKX",
                FileTypeFilter = new[] { new FilePickerFileType("Havok") { Patterns = new[] { "*.hkx" } } } });
            string? path = files.FirstOrDefault()?.TryGetLocalPath();
            if (path != null) OpenPhysics(path, out _);
        }
        catch (Exception error) { Say(error.Message); }
    }

    private HavokRagdollModel RequiredPhysics() => _physics ?? throw new InvalidOperationException("Open a ragdoll HKX first.");
    private int SelectedBody() => _physicsBodies.SelectedIndex >= 0 ? RequiredPhysics().Bodies[_physicsBodies.SelectedIndex].Id : throw new InvalidOperationException("Choose a body.");

    private void SetMass(bool all)
    {
        var model = RequiredPhysics();
        float mass = Values(_mass, 1)[0];
        if (mass < 0.0001f || mass > 100000) throw new ArgumentException("Mass must be between 0.0001 and 100000.");
        var skipped = WriteRagdollMasses(model, all, mass);
        ResetPhysics();
        foreach (var body in model.Bodies.Where(body => all || body.Id == SelectedBody()))
            if (!skipped.Contains(body.Id)) _masses[body.Id] = mass;
        Say((_ragdoll == null
                ? "Simulation mass set. Inertia will be calculated from the collision hull."
                : "Simulation mass set and stored in the file's inverse mass and inverse inertia. Save ragdoll " +
                  "values to write it.") + SkippedNote("body", skipped));
    }

    private IReadOnlyList<int> WriteRagdollMasses(HavokRagdollModel model, bool all, float mass)
    {
        if (_ragdoll is not { Ragdolls.Count: > 0 } document) return Array.Empty<int>();

        var edits = model.Bodies.Where(body => all || body.Id == SelectedBody())
                                .Select(body => (body.Id, mass))
                                .ToList();
        return document.SetBodyMasses(SelectedRagdoll(), edits).Skipped;
    }

    private static string SkippedNote(string what, IReadOnlyCollection<int> skipped) =>
        skipped.Count == 0
            ? ""
            : $" Skipped {skipped.Count} {what} row(s) this ragdoll does not hold: " +
              string.Join(", ", skipped.OrderBy(id => id).Take(10));

    private void SetJoints(bool all)
    {
        var model = RequiredPhysics();
        float min = Values(_angleMin, 1)[0], max = Values(_angleMax, 1)[0], cone = Values(_cone, 1)[0];
        if (min < -MathF.PI || max > MathF.PI || min > max || cone < 0 || cone > MathF.PI || _jointAxis.SelectedIndex == _jointRef.SelectedIndex)
            throw new ArgumentException("Choose distinct axes and ordered limits within [-pi, pi], with cone between 0 and pi.");
        if (!all && _physicsJoints.SelectedIndex < 0) throw new InvalidOperationException("Choose a joint.");
        var skipped = WriteRagdollLimits(model, all, min, max, cone);
        ResetPhysics();
        foreach (var joint in model.Constraints.Where((joint, i) => all || i == _physicsJoints.SelectedIndex))
        {
            if (skipped.Contains(joint.Id)) continue;

            _previewJoints[joint.Id] = new HavokConstraint { Id = joint.Id, Kind = joint.Kind, BodyA = joint.BodyA, BodyB = joint.BodyB,
                FrameA = joint.FrameA, FrameB = joint.FrameB, MinAngle = min, MaxAngle = max,
                TwistMinAngle = min, TwistMaxAngle = max, ConeMaxAngle = cone,
                LimitAxis = _jointAxis.SelectedIndex, TwistAxis = _jointAxis.SelectedIndex, TwistRefAxis = _jointRef.SelectedIndex,
                ConeTwistAxis = _jointAxis.SelectedIndex, ConeRefAxis = _jointAxis.SelectedIndex, HasPreviewLimits = true };
        }

        Say((_ragdoll == null
                ? "Preview joint settings applied. Source angular atoms are not imported or changed."
                : "Preview and file joint limits applied. The axes, motors and frames are not written; save " +
                  "ragdoll values to keep the limits.") + SkippedNote("constraint", skipped));
    }

    private IReadOnlyList<int> WriteRagdollLimits(HavokRagdollModel model, bool all, float min, float max, float cone)
    {
        if (_ragdoll is not { Ragdolls.Count: > 0 } document) return Array.Empty<int>();

        var node = CurrentRagdoll();
        var edits = new List<RagdollLimitEdit>();
        foreach (var joint in model.Constraints.Where((joint, i) => all || i == _physicsJoints.SelectedIndex))
        {
            bool coneRow = node != null && joint.Id >= 0 && joint.Id < node.Constraints.Count &&
                           node.Constraints[joint.Id].IsRagdoll;
            edits.Add(new RagdollLimitEdit(joint.Id, min, max, coneRow ? cone : null));
        }

        return document.SetConstraintLimits(SelectedRagdoll(), edits).Skipped;
    }

    private void EnsureSimulation()
    {
        if (_simulation != null) return;
        var source = RequiredPhysics();
        var preview = new HavokRagdollModel();
        preview.Shapes.AddRange(source.Shapes); preview.Bodies.AddRange(source.Bodies);
        foreach (var joint in source.Constraints)
        {
            if (!_previewJoints.TryGetValue(joint.Id, out var settings))
                throw new InvalidOperationException($"Set explicit preview settings for joint {joint.Id} first.");
            preview.Constraints.Add(settings);
        }
        _simulation = new RagdollSimulation(preview, _masses, Values(_gravity, 1)[0], Values(_ground, 1)[0]);
        _gravity.IsReadOnly = _ground.IsReadOnly = true;
        _physicsView.ShowSimulationFrames(_simulation.Frames);
        _physicsView.ShowMesh(_simulation.HullEdges);
        _physicsView.Frame();
    }

    private void StartPhysics()
    {
        EnsureSimulation();
        _physicsClock.Restart(); _lastTick = 0;
        _physicsTimer.Start();
        Say("Physics simulation running. Pause, step, push a body or reset to the source pose.");
    }

    private void PausePhysics() { _physicsTimer.Stop(); _physicsClock.Stop(); }
    private void AdvancePhysics(float seconds) { _simulation!.Advance(seconds); _physicsView.ShowSimulationFrames(_simulation.Frames); _physicsView.ShowMesh(_simulation.HullEdges); }
    private void StopPhysics() { PausePhysics(); _simulation?.Dispose(); _simulation = null; _gravity.IsReadOnly = _ground.IsReadOnly = false; }
    private void ResetPhysics() { StopPhysics(); _physicsView.ShowMesh(null); _physicsView.ShowSimulationFrames(null); if (_physics != null) _physicsView.SetBodies(_physics); }
}
