using System;
using System.IO;
using BehaviourStudio.App;

namespace BehaviourStudio.UiSmoke;

public static partial class Smoke
{    private static void PasteOnACopy(MainWindow window, string path, string name)
    {
        string copy = System.IO.Path.Combine(
            System.IO.Path.GetTempPath(), "uismoke-paste-" + System.IO.Path.GetFileName(path));

        try
        {
            System.IO.File.Copy(path, copy, true);
            System.IO.File.Delete(copy + ".bak");
        }
        catch (System.IO.IOException) { return; }

        window.Open(copy);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        string state = OpenCommonwealth.Services.Hkx.HkxTextEdit
            .IdsOfClass(window.LoadedXml, "hkbStateMachineStateInfo").FirstOrDefault() ?? "";
        if (state.Length == 0) return;

        CheckTrue($"{name}: paste is not offered until something has been copied", !window.CanPaste);

        window.SelectNode(state);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        CheckTrue($"{name}: the slot list offers leaving a paste unattached",
                  window.PasteSlots.Contains("(leave it unattached)"));
        CheckTrue($"{name}: and a slot on the selected node to hang it off",
                  window.PasteSlots.Count > 1);

        int was = new OpenCommonwealth.Services.Hkx.PackfileObjects(
            OpenCommonwealth.Services.Hkx.PackfileImage.Read(copy)).Instances.Count;

        window.CopyForTest();
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        Console.WriteLine("        " + window.ClipSummary);
        CheckTrue($"{name}: copying says what it took", window.ClipSummary.StartsWith("Holding #", StringComparison.Ordinal));
        CheckTrue($"{name}: and paste is offered afterwards", window.CanPaste);

        window.PasteForTest("(leave it unattached)");
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        Console.WriteLine("        " + window.PasteAnswer);
        CheckTrue($"{name}: pasting keeps the file before it as a .bak", System.IO.File.Exists(copy + ".bak"));
        CheckTrue($"{name}: and says what it pasted",
                  window.PasteAnswer.Contains("object(s) copied", StringComparison.Ordinal));

        int now = new OpenCommonwealth.Services.Hkx.PackfileObjects(
            OpenCommonwealth.Services.Hkx.PackfileImage.Read(copy)).Instances.Count;
        CheckTrue($"{name}: and the file on disk holds more objects than it did ({was} to {now})", now > was);

        TemplatesOnACopy(window, copy, name);
    }

    private static void TemplatesOnACopy(MainWindow window, string copy, string name)
    {

        string folder = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "uismoke-templates");
        if (System.IO.Directory.Exists(folder)) System.IO.Directory.Delete(folder, true);
        OpenCommonwealth.Services.Hkx.TemplateStore.Folder = folder;

        string clip = OpenCommonwealth.Services.Hkx.HkxTextEdit
            .IdsOfClass(window.LoadedXml, "hkbClipGenerator").FirstOrDefault() ?? "";
        if (clip.Length == 0) return;

        window.SelectNode(clip);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        window.SaveTemplateForTest("Smoke Shape");
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.PasteAnswer);

        if (window.TemplateNames.Count == 0)
        {
            CheckTrue($"{name}: a shape that cannot leave its file says so and names what it shares",
                      window.PasteAnswer.Contains("shares", StringComparison.Ordinal) &&
                      window.PasteAnswer.Contains("owns everything below it", StringComparison.Ordinal));
            return;
        }

        CheckTrue($"{name}: keeping a shape puts it in the template list", window.TemplateNames.Count == 1);
        CheckTrue($"{name}: and offers to apply it", window.CanApplyTemplate);

        window.ChooseTemplateForTest(window.TemplateNames[0]);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.PasteAnswer);

        CheckTrue($"{name}: choosing one says whether it fits this file before anything is applied",
                  window.PasteAnswer.Contains("already declared here", StringComparison.Ordinal) ||
                  window.PasteAnswer.Contains("Before this can go in", StringComparison.Ordinal));

        int was = new OpenCommonwealth.Services.Hkx.PackfileObjects(
            OpenCommonwealth.Services.Hkx.PackfileImage.Read(copy)).Instances.Count;

        window.ApplyTemplateForTest();
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();
        Console.WriteLine("        " + window.PasteAnswer);

        int now = new OpenCommonwealth.Services.Hkx.PackfileObjects(
            OpenCommonwealth.Services.Hkx.PackfileImage.Read(copy)).Instances.Count;

        if (window.PasteAnswer.StartsWith("Applied", StringComparison.Ordinal))
            CheckTrue($"{name}: applying a template adds objects to the file ({was} to {now})", now > was);
        else
            CheckTrue($"{name}: or says what to declare first and leaves the file alone",
                      window.PasteAnswer.Contains("declare", StringComparison.Ordinal) && now == was);
    }

    private static void SaveOnACopy(MainWindow window, string path, string name)
    {
        string copy = System.IO.Path.Combine(
            System.IO.Path.GetTempPath(), "uismoke-save-" + System.IO.Path.GetFileName(path));

        try
        {
            System.IO.File.Copy(path, copy, true);
            System.IO.File.Delete(copy + ".bak");
        }
        catch (System.IO.IOException) { return; }

        window.Open(copy);
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        if (!window.PickFrame(0, 0)) return;

        window.TypeFramePosition("7.25, -8.5, 9.75");
        window.PressSaveAnimation();
        Avalonia.Threading.Dispatcher.UIThread.RunJobs();

        Console.WriteLine("        " + window.FrameEditAnswer);

        CheckTrue($"{name}: saving keeps the original as a .bak", System.IO.File.Exists(copy + ".bak"));
        CheckTrue($"{name}: and says the file was written",
                  window.FrameEditAnswer.StartsWith("Saved ", StringComparison.Ordinal));
        CheckTrue($"{name}: and nothing is left unsaved afterwards", !window.AnimationEdited);

        var before = new OpenCommonwealth.Services.Hkx.HkxBinaryReader().ReadAnimation(path);
        var written = new OpenCommonwealth.Services.Hkx.HkxBinaryReader().ReadAnimation(copy);

        bool wasSpline = before.AnimationClass == "hkaSplineCompressedAnimation";
        Check($"{name}: the saved file keeps its kind where it can be re-encoded",
              wasSpline ? "hkaSplineCompressedAnimation" : "hkaInterleavedUncompressedAnimation",
              written.AnimationClass);

        float editLimit = wasSpline ? 0.05f : 0.001f;
        float elsewhereLimit = wasSpline ? 0.05f : 0.001f;

        var landed = written.Tracks[0].Translations[0];
        float editDrift = (landed - new System.Numerics.Vector3(7.25f, -8.5f, 9.75f)).Length();
        CheckTrue($"{name}: and holds the frame that was typed ({editDrift:F4})", editDrift < editLimit);

        float worst = 0;
        for (int t = 0; t < before.NumTracks; t++)
            for (int f = 0; f < before.NumFrames; f++)
            {
                if (t == 0 && f == 0) continue;
                worst = Math.Max(worst,
                    (before.Tracks[t].Translations[f] - written.Tracks[t].Translations[f]).Length());
            }

        Console.WriteLine($"        saved {written.AnimationClass}, every other frame moved at most {worst:E2}");
        CheckTrue($"{name}: leaving every other frame where it was ({worst:F4})", worst < elsewhereLimit);
    }

}
