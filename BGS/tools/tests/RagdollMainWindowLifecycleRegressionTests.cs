using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Interactivity;
using Avalonia.Threading;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class RagdollMainWindowLifecycleRegressionTests
{
    private const string Animation =
        "Meshes/Actors/Character/Animations/Paired/PairedKill2HMBashKneeAndHead_AttackerLead.hkx";
    private static bool _avaloniaReady;

    [Fact]
    public void RagdollPlaybackStateIsOwnedByTheOpenFile()
    {
        EnsureAvalonia();
        var failures = new List<string>();
        var window = new MainWindow();
        window.Show();

        try
        {
            window.Open(Fixture(Animation));
            Dispatcher.UIThread.RunJobs();

            var ragdoll = Field<HavokRagdollModel?>(window, "_ragdoll");
            var play = Field<Button>(window, "_playButton");
            var drop = Field<Button>(window, "_dropButton");
            var engineer = Field<CheckBox>(window, "_engineerToggle");
            var bodies = Field<CheckBox>(window, "_bodiesToggle");
            var constraints = Field<CheckBox>(window, "_constraintsToggle");
            var bindings = Field<CheckBox>(window, "_bindingsToggle");
            var physicsSkeleton = Field<CheckBox>(window, "_physicsSkeletonToggle");
            var frames = Field<CheckBox>(window, "_framesToggle");

            if (ragdoll is not { Bodies.Count: > 0 })
                failures.Add("the vanilla paired animation did not resolve its sibling ragdoll");
            if (!drop.IsEnabled)
                failures.Add("Drop was not enabled for the resolved ragdoll");
            if (!window.ScaleStatusVisibleForTest)
                failures.Add("the ragdoll scale status was not visible after load");

            Click(play);
            if (!window.IsPlaying)
                failures.Add("the vanilla paired animation did not start playback");
            Click(drop);
            if (!window.Viewport.IsDropped)
                failures.Add("Drop did not capture the displayed mapped pose");
            if (window.IsPlaying)
                failures.Add("Drop left normal animation playback running instead of freezing the release frame");
            if (window.Viewport.IsDropped) Click(drop);
            window.ScrubTo(window.PoseFrame);

            string sourceSummary = window.PlaybackSummary;
            if (!sourceSummary.Contains("loaded from the sibling skeleton", StringComparison.Ordinal))
                failures.Add("the sibling ragdoll source note was not present after opening the animation");
            else if (ragdoll is { Constraints.Count: > 0 })
            {
                int constraintId = ragdoll.Constraints[0].Id;
                window.Viewport.HoverConstraintForTest(constraintId);
                window.Viewport.HoverConstraintForTest(-1);
                if (!window.PlaybackSummary.Contains("loaded from the sibling skeleton", StringComparison.Ordinal))
                    failures.Add("constraint inspection discarded the sibling ragdoll source note when it restored the summary");
            }
            else
            {
                failures.Add("the resolved sibling ragdoll had no constraint available for the summary lifecycle check");
            }

            engineer.IsChecked = true;
            Dispatcher.UIThread.RunJobs();
            if (!window.Viewport.EngineeringFrames)
                failures.Add("Frames only did not enable engineering rendering before reset");

            Invoke(window, "ClearPose");
            Dispatcher.UIThread.RunJobs();

            if (Field<HavokRagdollModel?>(window, "_ragdoll") != null)
                failures.Add("ClearPose retained the previous file's ragdoll model");
            if (Field<string?>(window, "_ragdollSourceName") != null ||
                Field<string?>(window, "_ragdollSourceNote") != null)
                failures.Add("ClearPose retained the previous file's ragdoll source state");
            if (engineer.IsChecked == true || window.Viewport.EngineeringFrames)
                failures.Add("Frames only checkbox and viewport state were not both reset");
            foreach (var toggle in new[] { bodies, constraints, bindings, physicsSkeleton, frames, engineer })
                if (toggle.IsEnabled || toggle.IsChecked == true)
                    failures.Add($"{toggle.Content} retained enabled/checked state after ClearPose");
            if (drop.IsEnabled || drop.Content?.ToString() != "Drop")
                failures.Add("Drop retained state after ClearPose");
            if (window.ScaleStatusVisibleForTest)
                failures.Add("the previous ragdoll scale status survived ClearPose");
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }

        Assert.True(failures.Count == 0, string.Join(Environment.NewLine, failures));
    }

    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    private static void EnsureAvalonia()
    {
        if (_avaloniaReady) return;
        AppBuilder.Configure<RagdollTestApp>()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions())
            .SetupWithoutStarting();
        _avaloniaReady = true;
    }

    private static void Click(Button button) =>
        button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));

    private static T Field<T>(MainWindow window, string name) =>
        (T)typeof(MainWindow).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(window)!;

    private static void Invoke(MainWindow window, string name) =>
        typeof(MainWindow).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(window, null);
}

public sealed class RagdollTestApp : Application
{
    public override void Initialize() => Styles.Add(new Avalonia.Themes.Fluent.FluentTheme());
}
