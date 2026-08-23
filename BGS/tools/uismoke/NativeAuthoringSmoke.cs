using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Threading;
using BehaviourStudio.App;
using AppHost = BehaviourStudio.App.App;

namespace BehaviourStudio.UiSmoke;

// Production builds the window through AppHost.CreateMainWindow, which attaches the native
// authoring strip. The rest of this harness constructs MainWindow directly, so nothing
// here used to reach the strip or BatchAuthoringWindow at all. These cases go through the
// production factory so a regression in that path cannot hide behind a compile-only check.
internal static class NativeAuthoringSmoke
{
    internal static void Run()
    {
        StripAttachesOnceOnTheProductionPath();
        BatchAuthoringListsTheMachinesInTheDocument();
        ApplyIsOneUndoStepAndTouchesNoFile();
        ADocumentChangeInvalidatesAPendingPreview();
        ReopeningTheSourceInvalidatesAPendingPreview();
        SaveAfterApplyStillGoesThroughTheVerifiedTransaction();
    }

    private static void StripAttachesOnceOnTheProductionPath()
    {
        Console.WriteLine("\nthe native authoring strip attaches once on the production path");
        var window = AppHost.CreateMainWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();

        Smoke.Check("the production factory attaches the strip", 1,
                    Smoke.Find<NativeAuthoringStrip>(window).Count);
        Smoke.CheckTrue("and the batch authoring button is reachable from it",
                        Smoke.Find<Button>(window).Any(b => b.Content?.ToString() == "Batch authoring"));

        NativeAuthoringUi.Attach(window);
        Dispatcher.UIThread.RunJobs();
        Smoke.Check("attaching a second time does not stack a second strip", 1,
                    Smoke.Find<NativeAuthoringStrip>(window).Count);

        Smoke.CloseForTest(window);
    }

    private static void BatchAuthoringListsTheMachinesInTheDocument()
    {
        Console.WriteLine("\nbatch authoring lists the state machines in the open document");
        WithFixture((window, path) =>
        {
            var blank = AppHost.CreateMainWindow();
            blank.Show();
            Dispatcher.UIThread.RunJobs();
            var empty = NativeAuthoringUi.OpenBatchAuthoringForTest(blank);
            Dispatcher.UIThread.RunJobs();
            Smoke.Check("with no document open it offers no state machine", 0, empty.MachineNamesForTest.Count);
            empty.Close();
            Smoke.CloseForTest(blank);

            window.Open(path);
            Dispatcher.UIThread.RunJobs();

            var batch = NativeAuthoringUi.OpenBatchAuthoringForTest(window);
            Dispatcher.UIThread.RunJobs();

            Smoke.Check("the open document contributes its one state machine", 1,
                        batch.MachineNamesForTest.Count);
            Smoke.Check("named from the object it came from", "hkbStateMachine",
                        batch.MachineNamesForTest[0]);
            batch.Close();
        });
    }

    private static void ApplyIsOneUndoStepAndTouchesNoFile()
    {
        Console.WriteLine("\napply lands as one undo step and writes nothing to disk");
        WithFixture((window, path) =>
        {
            byte[] before = File.ReadAllBytes(path);
            window.Open(path);
            Dispatcher.UIThread.RunJobs();

            string opened = window.LoadedXml;
            var batch = Queue(window, ("Idle", "Animations\\Idle.hkx"), ("Walk", "Animations\\Walk.hkx"));
            Smoke.Check("both animations are queued", 2, batch.QueuedForTest);

            batch.PreviewForTest();
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("preview verifies and offers apply: " + batch.SummaryForTest,
                            batch.HasPendingPreviewForTest && batch.ApplyOfferedForTest);
            Smoke.CheckTrue("preview alone leaves the file untouched",
                            File.ReadAllBytes(path).SequenceEqual(before));
            Smoke.CheckTrue("and leaves the document clean", !window.IsDirty);

            batch.ApplyForTest();
            Dispatcher.UIThread.RunJobs();

            Smoke.Check("apply lands as exactly one undo step", 1, window.UndoStepsForTest);
            Smoke.CheckTrue("it makes the document dirty", window.IsDirty);
            Smoke.CheckTrue("the file on disk is still untouched",
                            File.ReadAllBytes(path).SequenceEqual(before));
            Smoke.CheckTrue("and the states reached the document",
                            window.LoadedXml.Contains(">Idle<", StringComparison.Ordinal) &&
                            window.LoadedXml.Contains(">Walk<", StringComparison.Ordinal));
            Smoke.CheckTrue("apply is not offered again until the batch is previewed again",
                            !batch.ApplyOfferedForTest);

            window.UndoForTest();
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("one undo removes the whole batch", window.LoadedXml == opened);
            Smoke.Check("and leaves nothing behind to undo", 0, window.UndoStepsForTest);
            batch.Close();
        });
    }

    private static void ADocumentChangeInvalidatesAPendingPreview()
    {
        Console.WriteLine("\nchanging the document after a preview refuses the apply");
        WithFixture((window, path) =>
        {
            byte[] before = File.ReadAllBytes(path);
            window.Open(path);
            Dispatcher.UIThread.RunJobs();

            var batch = Queue(window, ("Idle", "Animations\\Idle.hkx"));
            batch.PreviewForTest();
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("the preview is pending", batch.HasPendingPreviewForTest);

            // any edit through the ordinary document path, made behind the batch window's back
            string edited = window.LoadedXml.Replace(
                "<hkparam name=\"userData\">0</hkparam>",
                "<hkparam name=\"userData\">1</hkparam>", StringComparison.Ordinal);
            Smoke.CheckTrue("the edit really changes the document", edited != window.LoadedXml);
            window.SetXmlForTest(edited);
            Dispatcher.UIThread.RunJobs();
            int steps = window.UndoStepsForTest;

            batch.ApplyForTest();
            Dispatcher.UIThread.RunJobs();

            Smoke.CheckTrue("apply is refused and says why: " + batch.SummaryForTest,
                            batch.SummaryForTest.Contains("changed after preview", StringComparison.Ordinal));
            Smoke.CheckTrue("the pending preview is dropped", !batch.HasPendingPreviewForTest);
            Smoke.Check("and no document change is made", steps, window.UndoStepsForTest);
            Smoke.CheckTrue("the file on disk is untouched", File.ReadAllBytes(path).SequenceEqual(before));
            batch.Close();
        });
    }

    private static void ReopeningTheSourceInvalidatesAPendingPreview()
    {
        Console.WriteLine("\nreopening the source after a preview refuses the apply");
        WithFixture((window, path) =>
        {
            window.Open(path);
            Dispatcher.UIThread.RunJobs();

            var batch = Queue(window, ("Idle", "Animations\\Idle.hkx"));
            batch.PreviewForTest();
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("the preview is pending", batch.HasPendingPreviewForTest);

            string other = Path.Combine(Path.GetTempPath(), $"bgs-authoring-other-{Guid.NewGuid():N}.hkx");
            File.WriteAllBytes(other, Smoke.OneMachineBytes());
            try
            {
                window.Open(other);
                Dispatcher.UIThread.RunJobs();

                batch.ApplyForTest();
                Dispatcher.UIThread.RunJobs();

                Smoke.CheckTrue("apply is refused after the document was swapped: " + batch.SummaryForTest,
                                batch.SummaryForTest.Contains("changed after preview", StringComparison.Ordinal));
                Smoke.CheckTrue("the pending preview is dropped", !batch.HasPendingPreviewForTest);
                Smoke.Check("and the newly opened document is untouched", 0, window.UndoStepsForTest);
            }
            finally { File.Delete(other); }
            batch.Close();
        });
    }

    private static void SaveAfterApplyStillGoesThroughTheVerifiedTransaction()
    {
        Console.WriteLine("\nsave after apply still runs the verified save transaction");
        WithFixture((window, path) =>
        {
            byte[] before = File.ReadAllBytes(path);
            window.Open(path);
            Dispatcher.UIThread.RunJobs();

            var batch = Queue(window, ("Idle", "Animations\\Idle.hkx"));
            batch.PreviewForTest();
            Dispatcher.UIThread.RunJobs();
            batch.ApplyForTest();
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("the batch applied", window.IsDirty);

            window.VerifyFaultForTest = () => new InvalidDataException("injected verification fault");
            window.SaveForTest();
            Dispatcher.UIThread.RunJobs();

            Smoke.CheckTrue("a verification fault blocks the save of authored bytes",
                            File.ReadAllBytes(path).SequenceEqual(before));
            Smoke.CheckTrue("and leaves the authored document dirty", window.IsDirty);

            window.VerifyFaultForTest = null;
            window.SaveForTest();
            Dispatcher.UIThread.RunJobs();

            Smoke.CheckTrue("with verification passing the authored bytes reach disk",
                            !File.ReadAllBytes(path).SequenceEqual(before));
            Smoke.CheckTrue("and the document is no longer dirty", !window.IsDirty);

            var reopened = AppHost.CreateMainWindow();
            reopened.Show();
            reopened.Open(path);
            Dispatcher.UIThread.RunJobs();
            Smoke.CheckTrue("the saved file reopens with the authored state in it",
                            reopened.LoadedXml.Contains(">Idle<", StringComparison.Ordinal));
            Smoke.CloseForTest(reopened);
            batch.Close();
        });
    }

    private static BatchAuthoringWindow Queue(MainWindow window, params (string State, string Animation)[] entries)
    {
        var batch = NativeAuthoringUi.OpenBatchAuthoringForTest(window);
        Dispatcher.UIThread.RunJobs();
        batch.RefreshMachinesForTest();
        foreach (var (state, animation) in entries) batch.QueueForTest(state, animation);
        Dispatcher.UIThread.RunJobs();
        return batch;
    }

    private static void WithFixture(Action<MainWindow, string> test)
    {
        string path = Path.Combine(Path.GetTempPath(), $"bgs-authoring-{Guid.NewGuid():N}.hkx");
        File.WriteAllBytes(path, Smoke.OneMachineBytes());
        var window = AppHost.CreateMainWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();
        try { test(window, path); }
        finally
        {
            Smoke.CloseForTest(window);
            File.Delete(path);
        }
    }
}
