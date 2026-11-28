using System;
using System.Collections.Generic;
using System.Linq;

namespace OpenCommonwealth.Services.Archive;

public sealed record AnimationIndexGroup(
    AnimationKind Kind,
    string Heading,
    IReadOnlyList<AnimationIndexEntry> Entries,
    int Total);

public static class AnimationIndexView
{
    public const int MaxGridRows = 2000;

    public const int MaxFilterValues = 400;

    public static IReadOnlyList<AnimationIndexGroup> Groups(IReadOnlyList<AnimationIndexEntry> shown,
                                                            int budget = MaxGridRows)
    {
        var buckets = new List<(AnimationKind Kind, List<AnimationIndexEntry> Entries)>();
        foreach (AnimationKind kind in Enum.GetValues<AnimationKind>())
        {
            var bucket = new List<AnimationIndexEntry>();
            foreach (var entry in shown)
                if (entry.Kind == kind) bucket.Add(entry);
            if (bucket.Count > 0) buckets.Add((kind, bucket));
        }

        var plan = new List<AnimationIndexGroup>(buckets.Count);
        if (buckets.Count == 0) return plan;

        int room = budget < 0 ? 0 : budget;
        var taken = new int[buckets.Count];
        bool handedOut = true;
        while (room > 0 && handedOut)
        {
            handedOut = false;
            for (int i = 0; i < buckets.Count && room > 0; i++)
            {
                if (taken[i] >= buckets[i].Entries.Count) continue;
                taken[i]++;
                room--;
                handedOut = true;
            }
        }

        for (int i = 0; i < buckets.Count; i++)
        {
            int total = buckets[i].Entries.Count;
            IReadOnlyList<AnimationIndexEntry> rows = taken[i] >= total
                ? buckets[i].Entries
                : buckets[i].Entries.GetRange(0, taken[i]);
            plan.Add(new AnimationIndexGroup(buckets[i].Kind, Heading(buckets[i].Kind, taken[i], total), rows, total));
        }
        return plan;
    }

    public static int ShownCount(IReadOnlyList<AnimationIndexGroup> groups)
    {
        int count = 0;
        foreach (var group in groups) count += group.Entries.Count;
        return count;
    }

    public static IReadOnlyList<string> CappedValues(IEnumerable<string> values, string all, int cap, out int hidden)
    {
        var candidates = values as ICollection<string> ?? values.ToList();
        int take = cap < 0 ? 0 : cap;
        hidden = candidates.Count > take ? candidates.Count - take : 0;

        var items = new List<string>(Math.Min(take, candidates.Count) + 2) { all };
        int added = 0;
        foreach (string value in candidates)
        {
            if (added >= take) break;
            items.Add(value);
            added++;
        }

        if (hidden > 0) items.Add($"(+{hidden} more, use search)");
        return items;
    }

    public static string FilterValue(string selected, string all) =>
        string.IsNullOrEmpty(selected) || selected[0] == '(' ? all : selected;

    private static string Heading(AnimationKind kind, int shown, int total)
    {
        string heading = $"{AnimationIndex.GroupLabel(kind)} ({total})";
        return shown >= total ? heading : $"{heading}  (showing {shown} of {total})";
    }
}
