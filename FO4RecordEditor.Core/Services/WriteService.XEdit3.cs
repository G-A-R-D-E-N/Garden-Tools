using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;

namespace FO4RecordEditor.Services;

/// <summary>
/// xEdit parity, round 3: deep copy and reference retargeting.
/// </summary>
public static partial class WriteService
{
    // ── Deep copy as override ───────────────────────────────────────────────────────────────────

    /// <summary>
    /// A record plus every record it (transitively) references from the SAME source plugin, copied
    /// into a patch as overrides, so the copy is self-contained.
    ///
    /// This is deliberately NOT xEdit's "Deep copy as override into...". That command copies a
    /// record's CHILD GROUP -- xEdit's term for the placed references that live inside a CELL, or
    /// the cell blocks that live inside a WRLD (<c>IwbMainRecord.ChildGroup</c>, gated by
    /// <c>SelectionIncludesAnyDeepCopyRecords</c>, which only lights up for exactly those container
    /// types). It does not follow FormLinks at all. Mutagen has no equivalent of a "child group" for
    /// FO4's CELL/WRLD, and their placed-reference tree is already the tool's known problem area
    /// (<c>Fallout4ListGroup&lt;CellBlock&gt;</c> not implementing <c>IGroup</c> is why
    /// compact_to_esl refuses cell-nested records). Reproducing xEdit's actual behavior for CELL/WRLD
    /// is out of scope here; this refuses those two signatures rather than silently doing something
    /// else and calling it the same feature.
    ///
    /// What IS implemented, and is the commonly wanted case in practice: copying a custom item (a
    /// weapon, say) along with the custom keywords/effects/ammo it references, so the result does
    /// not dangle when the source mod is absent. Only links OWNED BY THE SOURCE PLUGIN are followed
    /// -- a link into Fallout4.esm or another already-loaded master is left alone, because that
    /// record already exists everywhere and copying it would be pointless and possibly harmful (its
    /// EditorID may not be unique in the target load position).
    ///
    /// Defaults to a dry run listing what would be copied.
    /// </summary>
    public static string DeepCopyAsOverride(object? env, string sourcePlugin, string id, string patchPlugin, bool apply, int cap = 200)
    {
        if (!ResolveFk(env, id, out var rootFk))
            return ToolError.Fail($"'{id}' is not a FormKey and no loaded record has that EditorID.");

        var rootVersions = MutagenLoader.GetRecordContexts(env, rootFk);
        var root = string.IsNullOrWhiteSpace(sourcePlugin)
            ? rootVersions.LastOrDefault().rec
            : MutagenLoader.GetRecordVersion(env, sourcePlugin, rootFk);
        if (root == null) return ToolError.Fail($"Could not find {rootFk} in '{sourcePlugin}'.");

        if (root.Registration.Name is "Cell" or "Worldspace")
            return ToolError.Fail(
                $"{root.Registration.Name} does not support deep copy here: xEdit's version copies its placed-" +
                "reference tree (CELL) or cell blocks (WRLD), which this tool cannot reproduce -- see " +
                "compact_to_esl's cell-nested-record refusal for why. Copy the cell with copy_as_override and " +
                "its placed objects individually with create_placed_object instead.");

        var owner = rootFk.ModKey;

        // BFS over FormLinks, following only links owned by the same plugin as the root.
        var visited = new HashSet<FormKey> { rootFk };
        var queue = new Queue<IMajorRecordGetter>();
        queue.Enqueue(root);
        var toCopy = new List<IMajorRecordGetter> { root };

        while (queue.Count > 0 && toCopy.Count < cap)
        {
            var cur = queue.Dequeue();
            foreach (var link in cur.EnumerateFormLinks())
            {
                var lfk = link.FormKey;
                if (lfk.IsNull || lfk.ModKey != owner || !visited.Add(lfk)) continue;

                var versions = MutagenLoader.GetRecordContexts(env, lfk);
                var target = versions.LastOrDefault().rec;
                if (target == null) continue;   // dangling; nothing to copy

                toCopy.Add(target);
                queue.Enqueue(target);
                if (toCopy.Count >= cap) break;
            }
        }

        if (!apply)
        {
            var byType = toCopy.GroupBy(r => r.Registration.Name).OrderByDescending(g => g.Count())
                .Select(g => $"{g.Key} x{g.Count()}");
            var capped = queue.Count > 0 ? $" (capped at {cap}; more remained in the queue)" : "";
            return $"DRY RUN: deep copy of {rootFk} would bring {toCopy.Count} record(s) into {patchPlugin}{capped}: " +
                   string.Join(", ", byType) + ". Re-run with apply=true.";
        }

        int ok = 0;
        var failures = new List<string>();
        // Copy children before the root is redundant to order strictly, but copying the root LAST
        // means every dependency it needs already exists in the patch when it lands, so an
        // AddOverride that happens to also validate outgoing links sees a complete picture.
        foreach (var rec in toCopy.AsEnumerable().Reverse())
        {
            var msg = ResolveConflict(env, rec.FormKey.ToString(), rec.FormKey.ModKey.FileName, patchPlugin);
            if (msg.StartsWith("Copied", StringComparison.OrdinalIgnoreCase)) ok++;
            else failures.Add($"{rec.FormKey} ({rec.Registration.Name}): {msg}");
        }

        var (patchName, _) = NormalizePlugin(patchPlugin);
        var result = $"Deep-copied {ok} of {toCopy.Count} record(s) rooted at {rootFk} into {patchName}. " +
                     $"Ensure {patchName} loads after the conflicting plugins, then save it.";
        if (failures.Count > 0) result += $" {failures.Count} failed: {string.Join("; ", failures.Take(5))}.";
        return result;
    }

    // ── Change Referencing Records ──────────────────────────────────────────────────────────────

    /// <summary>
    /// xEdit's "Change Referencing Records": point everything that references
    /// <paramref name="fromId"/> at <paramref name="toId"/> instead, across the load order. xEdit's
    /// own implementation (<c>CompareExchangeFormID</c>) rewrites links directly on records that are
    /// already editable; ours copies each referencing record's winning version into the patch as an
    /// override first (the same step copy_as_override does), then calls <c>RemapLinks</c> on that
    /// override -- the same single-record remap <c>IMajorRecord</c> already exposes, used
    /// elsewhere for a whole plugin's worth of renumbering.
    ///
    /// This is how you retire a duplicate record without leaving dangling links behind: point
    /// everything at the record you're keeping, delete the duplicate, done.
    ///
    /// Defaults to a dry run listing which records would change.
    /// </summary>
    public static string ChangeReferencingRecords(object? env, string fromId, string toId, string patchPlugin, bool apply, int cap = 300)
    {
        if (!ResolveFk(env, fromId, out var fromFk))
            return ToolError.Fail($"'{fromId}' is not a FormKey and no loaded record has that EditorID.");
        if (!ResolveFk(env, toId, out var toFk))
            return ToolError.Fail($"'{toId}' is not a FormKey and no loaded record has that EditorID.");
        if (fromFk == toFk) return ToolError.Fail("'from' and 'to' are the same record.");

        var referencing = MutagenLoader.GetReferencedBy(env, fromFk.ToString(), cap);
        if (referencing.Count == 0)
            return $"Nothing references {fromFk}; nothing to change.";

        if (!apply)
        {
            var preview = referencing.Take(50)
                .Select(r => $"  {r.Type} {r.FormKey} '{r.EditorID}' in {r.Plugin}");
            var more = referencing.Count > 50 ? $"\n  ... and {referencing.Count - 50} more" : "";
            return $"DRY RUN: {referencing.Count} record(s) reference {fromFk}; would repoint them at {toFk} in " +
                   $"{patchPlugin}:\n" + string.Join("\n", preview) + more + "\nRe-run with apply=true.";
        }

        var remap = new Dictionary<FormKey, FormKey> { [fromFk] = toFk };
        int ok = 0;
        var failures = new List<string>();

        foreach (var r in referencing)
        {
            if (!FormKey.TryFactory(r.FormKey, out var rfk)) { failures.Add($"{r.FormKey}: not a FormKey"); continue; }

            var copyMsg = ResolveConflict(env, rfk.ToString(), r.Plugin, patchPlugin);
            if (!copyMsg.StartsWith("Copied", StringComparison.OrdinalIgnoreCase))
            { failures.Add($"{rfk} ({r.Plugin}): {copyMsg}"); continue; }

            var (patchName, _) = NormalizePlugin(patchPlugin);
            var patch = GetMutable(patchName);
            var ovr = patch == null ? null : FindMutableRecord(patch, rfk.ToString());
            if (ovr is not IFormLinkContainer container) { failures.Add($"{rfk}: override is not a FormLink container"); continue; }

            try { container.RemapLinks(remap); ok++; }
            catch (Exception ex) { failures.Add($"{rfk}: remap failed ({ex.Message})"); }
        }

        var (name, _) = NormalizePlugin(patchPlugin);
        MutagenLoader.InvalidateModIndex(name); NotifyChanged(name);
        var result = $"Repointed {ok} of {referencing.Count} referencing record(s) from {fromFk} to {toFk} in {name}. " +
                     $"Ensure {name} loads after the plugins it copied from, then save_plugin.";
        if (failures.Count > 0) result += $" {failures.Count} failed: {string.Join("; ", failures.Take(5))}.";
        return result;
    }
}
