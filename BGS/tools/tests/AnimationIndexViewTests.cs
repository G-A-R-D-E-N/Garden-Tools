using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AnimationIndexViewTests
{
    [Fact]
    public void EveryNonEmptyGroupGetsAFairShareOfTheBudget()
    {
        var shown = new List<AnimationIndexEntry>();
        for (int i = 0; i < 5000; i++) shown.Add(Entry(AnimationKind.Animation, i));
        for (int i = 0; i < 2; i++) shown.Add(Entry(AnimationKind.Behaviour, i));
        shown.Add(Entry(AnimationKind.Skeleton, 0));

        var groups = AnimationIndexView.Groups(shown, 10);

        Assert.Equal(3, groups.Count);
        Assert.Equal(10, AnimationIndexView.ShownCount(groups));
        Assert.Equal(7, groups.Single(group => group.Kind == AnimationKind.Animation).Entries.Count);
        Assert.Equal(2, groups.Single(group => group.Kind == AnimationKind.Behaviour).Entries.Count);
        Assert.Single(groups.Single(group => group.Kind == AnimationKind.Skeleton).Entries);
    }

    [Fact]
    public void ATruncatedGroupSaysHowMuchItIsShowing()
    {
        var shown = new List<AnimationIndexEntry>();
        for (int i = 0; i < 40; i++) shown.Add(Entry(AnimationKind.Animation, i));
        for (int i = 0; i < 4; i++) shown.Add(Entry(AnimationKind.Behaviour, i));

        var groups = AnimationIndexView.Groups(shown, 20);

        var animations = groups.Single(group => group.Kind == AnimationKind.Animation);
        Assert.Equal(AnimationIndex.GroupLabel(AnimationKind.Animation) + " (40)  (showing 16 of 40)",
                     animations.Heading);
        Assert.Equal(40, animations.Total);

        var behaviours = groups.Single(group => group.Kind == AnimationKind.Behaviour);
        Assert.Equal(AnimationIndex.GroupLabel(AnimationKind.Behaviour) + " (4)", behaviours.Heading);
        Assert.Equal(4, behaviours.Entries.Count);
    }

    [Fact]
    public void AnExhaustedBudgetStillNamesEveryGroup()
    {
        var shown = new List<AnimationIndexEntry>
        {
            Entry(AnimationKind.Project, 0),
            Entry(AnimationKind.Skeleton, 1),
        };

        var groups = AnimationIndexView.Groups(shown, 0);

        Assert.Equal(2, groups.Count);
        Assert.Equal(0, AnimationIndexView.ShownCount(groups));
        Assert.All(groups, group => Assert.Contains("(showing 0 of 1)", group.Heading, StringComparison.Ordinal));
    }

    [Fact]
    public void TheDefaultBudgetShowsEverythingInAnOrdinaryIndex()
    {
        var shown = Enumerable.Range(0, 600).Select(index => Entry(AnimationKind.Animation, index)).ToList();

        var groups = AnimationIndexView.Groups(shown);

        Assert.Equal(600, AnimationIndexView.ShownCount(groups));
        Assert.Equal(AnimationIndex.GroupLabel(AnimationKind.Animation) + " (600)", groups[0].Heading);
    }

    [Fact]
    public void TheFilterValueListsAreCappedWithANote()
    {
        var values = Enumerable.Range(0, 405).Select(index => "Mod" + index).ToList();

        var items = AnimationIndexView.CappedValues(values, "all projects", AnimationIndexView.MaxFilterValues,
                                                    out int hidden);

        Assert.Equal(402, items.Count);
        Assert.Equal("all projects", items[0]);
        Assert.Equal(5, hidden);
        Assert.Equal("(+5 more, use search)", items[^1]);

        var small = AnimationIndexView.CappedValues(new[] { "A", "B" }, "all folders", 400, out int none);
        Assert.Equal(0, none);
        Assert.Equal(new[] { "all folders", "A", "B" }, small);
    }

    [Fact]
    public void AFilterNoteRowMeansNoFilter()
    {
        Assert.Equal("all projects", AnimationIndexView.FilterValue("(+5 more, use search)", "all projects"));
        Assert.Equal("all folders", AnimationIndexView.FilterValue("", "all folders"));
        Assert.Equal("Meshes/Actors", AnimationIndexView.FilterValue("Meshes/Actors", "all folders"));
    }

    private static AnimationIndexEntry Entry(AnimationKind kind, int index) =>
        new($"Meshes/Actors/Character/Animations/File{index}.hkx", "loose:Data", kind, "", "", "",
            "Meshes/Actors/Character/Animations", 1, Path.Combine("C:", "data"));
}
