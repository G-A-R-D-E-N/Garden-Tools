using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record IdenticalToMasterRow(string Key, string Signature, string? PreviousFilename);

public readonly record struct DeletedRecordCounts(int Records, int References, int Navmeshes);

public sealed record UnusedMasterList(IReadOnlyList<string> Names, bool Complete);

public readonly record struct EslEligibilityCounts(int NativeRecords, int OutOfRange, int Capacity);

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<IdenticalToMasterRow> GetIdenticalToMaster(string plugin, int limit = 0)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        if (limit < 0) throw new ArgumentOutOfRangeException(nameof(limit));
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvIdenticalToMaster(use.Pointer, plugin, (nuint)limit, &items, &count));
        return Call.TakeStrings(items, count).Select(ParseIdenticalRow).ToArray();
    }

    public DeletedRecordCounts GetDeletedCounts(string plugin)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        using var use = Use();
        nuint records = 0;
        nuint references = 0;
        nuint navmeshes = 0;
        Call.Check(RadicleNative.EnvDeletedCounts(use.Pointer, plugin, &records, &references, &navmeshes));
        return new DeletedRecordCounts(checked((int)records), checked((int)references), checked((int)navmeshes));
    }

    public UnusedMasterList GetUnusedMasters(string plugin)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        int complete = 0;
        Call.Check(RadicleNative.EnvUnusedMasters(use.Pointer, plugin, &items, &count, &complete));
        return new UnusedMasterList(Call.TakeStrings(items, count), complete != 0);
    }

    public EslEligibilityCounts GetEslEligibility(string plugin)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        using var use = Use();
        nuint nativeRecords = 0;
        nuint outOfRange = 0;
        nuint capacity = 0;
        Call.Check(RadicleNative.EnvEslEligibility(use.Pointer, plugin, &nativeRecords, &outOfRange, &capacity));
        return new EslEligibilityCounts(checked((int)nativeRecords), checked((int)outOfRange), checked((int)capacity));
    }

    private static IdenticalToMasterRow ParseIdenticalRow(string row)
    {
        var parts = row.Split('\t');
        if (parts.Length != 3) throw new InvalidDataException("Identical-to-master row must hold three tab-separated fields.");
        return new IdenticalToMasterRow(parts[0], parts[1], parts[2].Length == 0 ? null : parts[2]);
    }
}

public sealed record CheckFinding(string Code, string Severity, string Plugin, string Record, string Subrecord,
    string Offset, string Target, string Message);

public sealed record MissingAssetRow(string Plugin, string Record, string Path);

public sealed record RecordSearchQuery(string? Text = null, string? FormId = null, IReadOnlyList<string>? Signatures = null,
    string? EditorId = null, string? EditorIdPattern = null, string? DisplayName = null, string? FieldValue = null,
    uint ConflictCategoryMask = 0, int Limit = 0);

public sealed record RecordSearchResult(IReadOnlyList<string> Rows, bool Truncated);

public sealed record PrevisIssue(int Code, string Name, string Detail);

public sealed record PrevisCellReport(FormKey Cell, int PluginIndex, bool Broken, bool Outdated, bool SkipUnsupported,
    IReadOnlyList<PrevisIssue> Issues, IReadOnlyList<PrevisIssue> OutdatedIssues, IReadOnlyList<string> Skips);

public sealed record PrevisReport(int Cells, int Broken, int Outdated, int Skipped, int CdxFiles, int CdxRows, int CsgFiles,
    int CsgRows, int CsgChunks, int TomeFiles, int OcNifFiles, IReadOnlyList<string> Problems,
    IReadOnlyList<PrevisCellReport> CellReports);

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<CheckFinding> CheckErrors(string? plugin = null, int limit = 0)
    {
        if (limit < 0) throw new ArgumentOutOfRangeException(nameof(limit));
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvCheckErrors(use.Pointer, plugin, (nuint)limit, &items, &count));
        return Call.TakeStrings(items, count).Select(row =>
        {
            var parts = row.Split('\t', 8);
            if (parts.Length != 8) throw new InvalidDataException("A check finding must hold eight tab-separated fields.");
            return new CheckFinding(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6], parts[7]);
        }).ToArray();
    }

    public IReadOnlyList<MissingAssetRow> MissingAssets(AssetResolver assets, string? plugin = null)
    {
        ArgumentNullException.ThrowIfNull(assets);
        using var use = Use();
        using var assetUse = assets.Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvMissingAssets(use.Pointer, assetUse.Pointer, plugin, &items, &count));
        return Call.TakeStrings(items, count).Select(row =>
        {
            var parts = row.Split('\t', 3);
            if (parts.Length != 3) throw new InvalidDataException("A missing-asset row must hold three tab-separated fields.");
            return new MissingAssetRow(parts[0], parts[1], parts[2]);
        }).ToArray();
    }

    public IReadOnlyList<string> RecordJson(string plugin, string formKey, bool includeRaw = false)
    {
        ArgumentNullException.ThrowIfNull(plugin);
        ArgumentNullException.ThrowIfNull(formKey);
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvRecordJson(use.Pointer, plugin, formKey, includeRaw ? 1 : 0, &items, &count));
        return Call.TakeStrings(items, count);
    }

    public RecordSearchResult SearchRecords(RecordSearchQuery query, StringTables? strings = null)
    {
        ArgumentNullException.ThrowIfNull(query);
        if (query.Limit < 0) throw new ArgumentOutOfRangeException(nameof(query));
        if (strings is not null && strings.Owner != this)
            throw new ArgumentException("The string tables do not belong to this environment.", nameof(strings));
        using var use = Use();
        using var stringsUse = strings is null ? default : strings.Borrow();
        using var s = new NativeStrings();
        var signatures = s.AddAll(query.Signatures);
        IntPtr* items = null;
        nuint count = 0;
        int truncated = 0;
        fixed (IntPtr* signaturePointer = signatures)
        {
            var native = new Native.RecordSearch
            {
                Text = s.Add(query.Text), FormId = s.Add(query.FormId),
                Signatures = (IntPtr)signaturePointer, SignatureCount = (nuint)signatures.Length,
                EditorId = s.Add(query.EditorId), EditorIdPattern = s.Add(query.EditorIdPattern),
                DisplayName = s.Add(query.DisplayName), FieldValue = s.Add(query.FieldValue),
                ConflictCategoryMask = query.ConflictCategoryMask, Limit = (nuint)query.Limit,
            };
            Call.Check(strings is null
                ? RadicleNative.EnvSearchRecords(use.Pointer, &native, &items, &count, &truncated)
                : RadicleNative.EnvSearchRecordsWithStrings(use.Pointer, &native, stringsUse.Pointer, &items, &count, &truncated));
        }
        return new RecordSearchResult(Call.TakeStrings(items, count), truncated != 0);
    }

    public IReadOnlyList<string> DiffPlugins(string beforePlugin, string afterPlugin)
    {
        ArgumentNullException.ThrowIfNull(beforePlugin);
        ArgumentNullException.ThrowIfNull(afterPlugin);
        using var use = Use();
        IntPtr* items = null;
        nuint count = 0;
        Call.Check(RadicleNative.EnvDiffPlugins(use.Pointer, beforePlugin, afterPlugin, &items, &count));
        return Call.TakeStrings(items, count);
    }

    public PrevisReport PrevisScan(string dataDirectory)
    {
        ArgumentNullException.ThrowIfNull(dataDirectory);
        using var use = Use();
        IntPtr report;
        Call.Check(RadicleNative.PrevisScan(use.Pointer, dataDirectory, &report));
        try
        {
            Native.PrevisSummary summary;
            Call.Check(RadicleNative.PrevisReportSummary(report, &summary));
            var problems = new List<string>();
            for (nuint i = 0; i < summary.ProblemCount; ++i)
            {
                IntPtr detail;
                Call.Check(RadicleNative.PrevisReportProblem(report, i, &detail));
                problems.Add(Utf8.Read(detail) ?? string.Empty);
            }
            var cells = new List<PrevisCellReport>();
            var cellCount = RadicleNative.PrevisReportCellCount(report);
            for (nuint i = 0; i < cellCount; ++i)
            {
                Native.PrevisCell cell;
                Call.Check(RadicleNative.PrevisReportCell(report, i, &cell));
                var issues = new List<PrevisIssue>();
                for (nuint j = 0; j < cell.IssueCount; ++j)
                {
                    int code;
                    IntPtr detail;
                    Call.Check(RadicleNative.PrevisReportIssue(report, i, j, &code, &detail));
                    issues.Add(new PrevisIssue(code, Utf8.Read(RadicleNative.PrevisIssueName(code)) ?? "unknown", Utf8.Read(detail) ?? string.Empty));
                }
                var outdated = new List<PrevisIssue>();
                for (nuint j = 0; j < cell.OutdatedIssueCount; ++j)
                {
                    int code;
                    IntPtr detail;
                    Call.Check(RadicleNative.PrevisReportOutdatedIssue(report, i, j, &code, &detail));
                    outdated.Add(new PrevisIssue(code, Utf8.Read(RadicleNative.PrevisIssueName(code)) ?? "unknown", Utf8.Read(detail) ?? string.Empty));
                }
                var skips = new List<string>();
                for (nuint j = 0; j < cell.SkipCount; ++j)
                {
                    IntPtr detail;
                    Call.Check(RadicleNative.PrevisReportSkip(report, i, j, &detail));
                    skips.Add(Utf8.Read(detail) ?? string.Empty);
                }
                cells.Add(new PrevisCellReport(FormKey.Required(cell.Cell, "previs cell"), checked((int)cell.PluginIndex),
                    cell.Broken != 0, cell.Outdated != 0, cell.SkipUnsupported != 0, issues, outdated, skips));
            }
            return new PrevisReport(checked((int)summary.Cells), checked((int)summary.Broken), checked((int)summary.Outdated),
                checked((int)summary.Skipped), checked((int)summary.CdxFiles), checked((int)summary.CdxRows),
                checked((int)summary.CsgFiles), checked((int)summary.CsgRows), checked((int)summary.CsgChunks),
                checked((int)summary.TomeFiles), checked((int)summary.OcNifFiles), problems, cells);
        }
        finally { RadicleNative.PrevisReportDestroy(report); }
    }
}

public sealed unsafe partial class PluginFile
{
    /// <summary>Invariant: the result rows must be freed whatever the status.</summary>
    public IReadOnlyList<string> ApplyRecordJson(FormKey record, string json, uint options = 0)
    {
        ArgumentNullException.ThrowIfNull(json);
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr* items = null;
        nuint count = 0;
        var status = RadicleNative.PluginApplyRecordJson(use.Pointer, s.Key(record), json, options, &items, &count);
        var message = status == Status.Ok ? null : Utf8.Read(RadicleNative.LastError());
        var rows = Call.TakeStrings(items, count);
        if (status != Status.Ok)
            throw new RadicleException(status, (message ?? status.ToString()) + (rows.Length == 0 ? "" : ": " + string.Join("; ", rows)));
        return rows;
    }
}
