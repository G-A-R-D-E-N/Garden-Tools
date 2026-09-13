using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using Avalonia.Headless;
using Avalonia.Threading;
using BehaviourStudio.App;
using BehaviourStudio.UiSmoke;
using OpenCommonwealth.Services.Hkx;
using System.Numerics;

class Audit
{
    const BindingFlags HiddenStatic = BindingFlags.Static | BindingFlags.NonPublic;
    const BindingFlags HiddenInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    static readonly string Root = Directory.CreateTempSubdirectory("bgs-audit-d2ae05ba-").FullName;
    static byte[] Fixture(string name) => (byte[])typeof(Smoke).GetMethod(name, HiddenStatic)!.Invoke(null, null)!;
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); Console.WriteLine("REPRODUCED: " + message); }
    static MainWindow Open(string name, byte[] bytes)
    {
        string path = Path.Combine(Root, name + ".hkx");
        File.WriteAllBytes(path, bytes);
        var window = new MainWindow(); window.Show(); window.Open(path); Dispatcher.UIThread.RunJobs();
        return window;
    }
    static void Close(MainWindow window) { window.DiscardDecision = () => DiscardChoice.Discard; window.Close(); Dispatcher.UIThread.RunJobs(); }
    static void Main()
    {
        var settings = typeof(MainWindow).Assembly.GetType("BehaviourStudio.App.Settings")!;
        settings.GetProperty("SettingsPathForTest", HiddenStatic)!.SetValue(null, Path.Combine(Root, "settings.cfg"));
        File.WriteAllText(Path.Combine(Root, "settings.cfg"), "tour_done=1\n");
        AppBuilder.Configure<HeadlessApp>().UseHeadless(new AvaloniaHeadlessPlatformOptions()).SetupWithoutStarting();
        SaveValidation();
        MixedAnimation();
        PendingClose();
        SkeletonCount();
        CopySnapshot();
        ModlistScans();
        Console.WriteLine("All seven historical findings reproduced. Inputs: " + Root);
    }
    static void SaveValidation()
    {
        var batch = BatchAnimationBuilder.Build(Fixture("OneMachineBytes"), 90, new[] { new BatchAnimationBuilder.Entry("A", "a.hkx"), new BatchAnimationBuilder.Entry("B", "b.hkx") });
        var w = Open("save-validation", batch.Bytes);
        var states = StateEditor.States(BehaviourGraphModel.Parse(w.LoadedXml), "90");
        var changed = HkxTextEdit.SetParam(w.LoadedXml, states[1].Id, "stateId", states[0].StateId.ToString());
        w.SetXmlForTest(changed);
        var before = File.ReadAllBytes(w.PathFieldForTest);
        w.SaveForTest();
        Check(w.IsDirty && File.ReadAllBytes(w.PathFieldForTest).SequenceEqual(before), "toolbar refuses duplicate state IDs");
        string path = w.PathFieldForTest;
        w.DiscardDecision = () => DiscardChoice.Save; w.Close(); Dispatcher.UIThread.RunJobs();
        Check(!w.IsVisible && !File.ReadAllBytes(path).SequenceEqual(before) && GraphValidator.Check(HkxTextEdit.TextOf(path)).Any(f => f.What.Contains("is used by")), "Save and continue commits the same duplicate state IDs and closes");
    }
    static void MixedAnimation()
    {
        var scope = typeof(BehaviourStudio.Tests.AnimationSaveTransactionTests).GetNestedType("AnimationScope", BindingFlags.NonPublic)!;
        var made = ((byte[] Source, HkxAnimationData Animation))scope.GetMethod("CreateSource", HiddenStatic)!.Invoke(null, new object[] { true })!;
        var encoded = NativeAnimation.Recompress(made.Source, made.Animation).Bytes;
        var w = Open("mixed-animation", encoded);
        Console.WriteLine($"animation open: xml={w.LoadedXml.Length}, frames={w.AnimationFrameCount}");
        Check(w.PickFrame(0, 1), "animation fixture has editable frame");
        w.TypeFramePosition("123, 456, 789");
        var model = BehaviourGraphModel.Parse(w.LoadedXml);
        var binding = model.Objects.First(o => o.Class == "hkaAnimationBinding");
        w.SetXmlForTest(HkxTextEdit.SetParam(w.LoadedXml, binding.Id, "originalSkeletonName", "audit skeleton"));
        string path = w.PathFieldForTest;
        Check(w.IsDirty && w.AnimationEdited, "animation frame and metadata can both be dirty");
        w.DiscardDecision = () => DiscardChoice.Save; w.Close(); Dispatcher.UIThread.RunJobs();
        var reread = new HkxBinaryReader().ReadAnimation(path);
        Check(!w.IsVisible && reread.Tracks[0].Translations[1] != new Vector3(123, 456, 789) && HkxTextEdit.TextOf(path).Contains("audit skeleton"), "Save and continue keeps metadata but loses the edited animation frame");
    }
    static void PendingClose()
    {
        var w = Open("pending-close", Fixture("OneClipBytes"));
        w.GetLogicalDescendants().OfType<TabControl>().First().SelectedIndex = 2;
        w.SetGraphRightPaneOpen(true); w.SelectNode("90"); Dispatcher.UIThread.RunJobs();
        var textbox = w.GraphProperties.Body.GetLogicalDescendants().OfType<TextBox>().Single(t => t.Text == "2.5");
        Check(textbox.Focus(), "editable property textbox receives focus");
        textbox.Text = "0.75";
        var commits = (List<Action>)typeof(MainWindow).GetField("_fieldCommits", HiddenInstance)!.GetValue(w)!;
        bool committed = false;
        commits.Add(() => { committed = true; w.SetXmlForTest(HkxTextEdit.SetParam(w.LoadedXml, "90", "playbackSpeed", "0.75")); });
        int decisions = 0;
        w.DiscardDecision = () => { decisions++; return DiscardChoice.Cancel; };
        w.Close(); Dispatcher.UIThread.RunJobs();
        Check(!committed && decisions == 0 && !w.IsVisible, "close does not flush registered pending fields or consult discard decision");
    }
    static void SkeletonCount()
    {
        var image = new PackfileImage { Predicates = new byte[16] };
        foreach (var name in new[] { "__classnames__", "__types__", "__data__" }) { var tag = new byte[20]; System.Text.Encoding.ASCII.GetBytes(name).CopyTo(tag, 0); image.Sections.Add(new PackfileSection { TagBytes = tag }); }
        var obj = NativeAppend.Object(image, "hkaSkeleton");
        var data = image.Section("__data__")!;
        int bones = obj.Offset + HavokClasses.Shipped.Field("hkaSkeleton", "bones")!.Offset;
        data.SetLocal(bones, obj.Offset);
        BitConverter.GetBytes(100000).CopyTo(data.Data, bones + image.Layout.PointerSize);
        FixupOrder.Reorder(image);
        string path = Path.Combine(Root, "invalid-skeleton.hkx");
        File.WriteAllBytes(path, image.Rebuild());
        var skeleton = new HkxBinaryReader().ReadSkeleton(path);
        Check(skeleton.BoneNames.Count == 100000 && new FileInfo(path).Length < 2048, $"{new FileInfo(path).Length}-byte skeleton produces {skeleton.BoneNames.Count} bones from an invalid count");
    }
    static void CopySnapshot()
    {
        var batch = BatchAnimationBuilder.Build(Fixture("OneMachineBytes"), 90, new[] { new BatchAnimationBuilder.Entry("CopyMe", "a.hkx", PlaybackSpeed: 2.5f) });
        var w = Open("copy-snapshot", batch.Bytes);
        var clipId = BehaviourGraphModel.Parse(w.LoadedXml).Objects.First(o => o.Class == "hkbClipGenerator").Id;
        w.SetXmlForTest(HkxTextEdit.SetParam(w.LoadedXml, clipId, "playbackSpeed", "0.75"));
        w.SelectNode(clipId); w.CopyForTest(); Console.WriteLine(w.PasteAnswer);
        var clip = (NativePaste.Clip)typeof(MainWindow).GetField("_clip", HiddenStatic)!.GetValue(null)!;
        var properties = clip.GetType().GetProperties();
        Console.WriteLine("copied snapshot properties: " + string.Join(", ", properties.Select(p => p.Name)));
        string targetPath = Path.Combine(Root, "paste-target.hkx");
        File.WriteAllBytes(targetPath, Fixture("OneClipBytes"));
        var pasted = NativePaste.Paste(File.ReadAllBytes(targetPath), targetPath, clip);
        string xml = NativeXml.From(pasted.Bytes);
        var speed = HkxTextEdit.ReadParams(xml, pasted.RootId.ToString()).Single(p => p.Name == "playbackSpeed").Value;
        Check(speed == "2.5", "Copy subtree takes saved speed 2.5 instead of visible unsaved speed 0.75");
        string sourceXml = HkxTextEdit.TextOf(w.PathFieldForTest);
        var plan = NativeSave.Compare(sourceXml, HkxTextEdit.SetParam(sourceXml, clipId, "playbackSpeed", "1.25"));
        File.WriteAllBytes(w.PathFieldForTest, NativeSave.Apply(File.ReadAllBytes(w.PathFieldForTest), plan));
        pasted = NativePaste.Paste(File.ReadAllBytes(targetPath), targetPath, clip);
        speed = HkxTextEdit.ReadParams(NativeXml.From(pasted.Bytes), pasted.RootId.ToString()).Single(p => p.Name == "playbackSpeed").Value;
        Check(speed == "1.25", "clipboard changes after source is modified: paste uses 1.25 instead of copied 2.5");
        Close(w);
        }
    static void ModlistScans()
    {
        string corrupt = Path.Combine(Root, "mo2-unreadable");
        string chars = Path.Combine(corrupt, "mods", "AuditMod", "meshes", "actors", "audit", "characters");
        Directory.CreateDirectory(chars);
        Directory.CreateDirectory(Path.Combine(corrupt, "profiles", "Default"));
        File.WriteAllText(Path.Combine(corrupt, "profiles", "Default", "modlist.txt"), "+AuditMod\n");
        File.WriteAllBytes(Path.Combine(chars, "broken.hkx"), new byte[] { 0, 1, 2, 3 });
        Check(ModlistScan.ScanModlist(new[] { "--scan-modlist", corrupt }) == 0,
            "unreadable character file is skipped and scan returns success");

        string overrides = Path.Combine(Root, "mo2-overrides");
        Directory.CreateDirectory(Path.Combine(overrides, "profiles", "Default"));
        File.WriteAllText(Path.Combine(overrides, "profiles", "Default", "modlist.txt"), "+High\n+Low\n");
        foreach (string mod in new[] { "High", "Low" })
        {
            string folder = Path.Combine(overrides, "mods", mod, "meshes", "actors", "audit", "behaviors");
            Directory.CreateDirectory(folder);
            string source = mod == "High" ? "copy-snapshot.hkx" : "save-validation.hkx";
            File.Copy(Path.Combine(Root, source), Path.Combine(folder, "graph.hkx"));
        }
        File.WriteAllBytes(Path.Combine(overrides, "mods", "High", "meshes", "actors", "audit", "a.hkx"), new byte[] { 0 });
        Check(ModlistScan.ScanClips(new[] { "--scan-clips", overrides }) == 1,
            "overridden lower-priority behavior produces a missing-clip failure");
    }
}
