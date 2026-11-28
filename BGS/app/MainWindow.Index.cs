using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Archive;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    private const string AllKinds = "all kinds";
    private const string AllProjects = "all projects";
    private const string AllFolders = "all folders";

    private readonly HkGrid _indexGrid = new(("Path", -4), ("Source", 190), ("Kind", 92), ("Project", 150));
    private readonly TextBox _indexSearch =
        Ux.Field("words to look for, in any order, such as: character attack", 260);
    private readonly TextBox _indexDataField = Ux.Field("Path to the game Data folder", 210);
    private readonly ComboBox _indexKind = new()
    {
        MinWidth = 120, MinHeight = Ux.ControlHeight, Foreground = Ux.CodeBrush, FontSize = 12,
    };
    private readonly ComboBox _indexProject = new()
    {
        MinWidth = 150, MinHeight = Ux.ControlHeight, Foreground = Ux.CodeBrush, FontSize = 12,
    };
    private readonly ComboBox _indexFolder = new()
    {
        MinWidth = 190, MinHeight = Ux.ControlHeight, Foreground = Ux.CodeBrush, FontSize = 12,
    };
    private readonly TextBlock _indexSummary = new() { Foreground = Ux.MetaBrush, FontSize = 12 };
    private readonly TextBlock _indexProgress = new() { Foreground = Ux.MutedBrush, FontSize = 12 };

    private AnimationIndex? _animationIndex;
    private AnimationIndexEntry[] _indexShown = Array.Empty<AnimationIndexEntry>();
    private CancellationTokenSource? _indexCancellation;
    private int _indexGeneration;
    private bool _indexBuilding;
    private bool _indexFilterPaused;

    private Control BuildAnimationIndexTab()
    {
        _indexDataField.Text = Settings.Get("gameDataFolder");
        _indexDataField.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter) StartIndexBuild(force: false);
        };
        var browse = Ux.Secondary("Browse...");
        browse.Click += async (_, _) => await PickIndexDataFolder();
        var build = Ux.Secondary("Index now");
        build.Click += (_, _) => StartIndexBuild(force: false);
        var rebuild = Ux.Secondary("Rebuild");
        rebuild.Click += (_, _) => StartIndexBuild(force: true);
        var cancel = Ux.Secondary("Cancel");
        cancel.Click += (_, _) => _indexCancellation?.Cancel();
        var open = Ux.Secondary("Open selected");
        open.Click += (_, _) => OpenSelectedIndexEntry();

        RegisterToolbar("Animations",
            ToolbarGroup("Animation index", _indexDataField, browse, build, rebuild, cancel),
            ToolbarGroup("Find", _indexSearch, _indexKind, _indexProject, _indexFolder),
            ToolbarGroup("Entry", open));

        _indexSearch.PropertyChanged += (_, e) =>
        {
            if (e.Property == TextBox.TextProperty) ApplyIndexFilter();
        };
        _indexKind.SelectionChanged += (_, _) => ApplyIndexFilter();
        _indexProject.SelectionChanged += (_, _) => ApplyIndexFilter();
        _indexFolder.SelectionChanged += (_, _) => ApplyIndexFilter();

        _indexGrid.SelectionChanged += UpdateIndexStatus;
        _indexGrid.BodyTree.DoubleTapped += (_, _) => OpenSelectedIndexEntry();
        _indexGrid.BodyTree.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter) OpenSelectedIndexEntry();
        };

        ToolTip.SetTip(_indexGrid, "Double-click an entry to open it; archive entries open read only");
        ResetIndexFilters(Array.Empty<AnimationIndexEntry>());
        _indexSummary.Text = "no index built yet";
        _indexProgress.Text = "press Index now to scan the game data, its archives and the modlist";

        var panel = new DockPanel();
        AddTop(panel, Ux.Wrap(Ux.Pill(_indexSummary), Ux.Pill(_indexProgress)));
        panel.Children.Add(_indexGrid);
        return panel;
    }

    private async Task PickIndexDataFolder()
    {
        var picked = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "The game Data folder (holds the .ba2 archives)",
            AllowMultiple = false,
        });

        string? folder = picked.Count > 0 ? picked[0].TryGetLocalPath() : null;
        if (folder == null) return;
        _indexDataField.Text = folder;
        StartIndexBuild(force: false);
    }

    private AnimationIndexRequest? IndexRequestFor(string folder, out string failure)
    {
        failure = "";
        string data = folder.Trim();
        if (data.Length == 0) data = Settings.Get("gameDataFolder");
        if (data.Length == 0 || !Directory.Exists(data))
        {
            failure = "set the game Data folder first";
            return null;
        }

        Settings.TrySet("gameDataFolder", data, out _);
        return DiscoverIndexRequest(data, Settings.Get("gameModsFolder"), out failure);
    }

    private static AnimationIndexRequest? DiscoverIndexRequest(string data, string mods, out string failure)
    {
        failure = "";
        try
        {
            return AnimationIndexRequest.FromLoadOrder(data, mods.Length > 0 ? mods : null);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException
                                       or NotSupportedException)
        {
            failure = "the load order could not be read: " + e.Message.Split('\n')[0];
            return null;
        }
    }

    private async void StartIndexBuild(bool force)
    {
        if (_indexBuilding)
        {
            _indexProgress.Text = "an index build is already running";
            _indexProgress.Foreground = Ux.WarnBrush;
            return;
        }

        string data = (_indexDataField.Text ?? "").Trim();
        if (data.Length == 0) data = Settings.Get("gameDataFolder");
        if (data.Length == 0 || !Directory.Exists(data))
        {
            _indexProgress.Text = "set the game Data folder first";
            _indexProgress.Foreground = Ux.WarnBrush;
            return;
        }

        Settings.TrySet("gameDataFolder", data, out _);
        string mods = Settings.Get("gameModsFolder");

        _indexBuilding = true;
        int generation = ++_indexGeneration;
        var cancellation = new CancellationTokenSource();
        _indexCancellation = cancellation;
        _indexProgress.Foreground = Ux.MutedBrush;
        _indexProgress.Text = "indexing...";
        IProgress<AnimationIndexProgress> progress = new Progress<AnimationIndexProgress>(
            step => ShowIndexProgress(step, generation));

        try
        {
            string failure = "";
            var index = await Task.Run(() =>
            {
                var request = DiscoverIndexRequest(data, mods, out failure);
                if (request == null) return null;

                string cache = AnimationIndex.DefaultCachePathFor(request);
                var options = force ? AnimationIndexOptions.Fresh(cache) : AnimationIndexOptions.Cached(cache);
                return AnimationIndex.Build(request, options, progress.Report, cancellation.Token);
            }, cancellation.Token);

            if (index == null)
            {
                _indexProgress.Text = failure;
                _indexProgress.Foreground = Ux.WarnBrush;
                return;
            }

            ApplyIndex(index);
        }
        catch (OperationCanceledException)
        {
            _indexProgress.Text = "indexing cancelled; the previous index is unchanged";
            _indexProgress.Foreground = Ux.WarnBrush;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException
                                       or ArgumentException)
        {
            _indexProgress.Text = "the index could not be built: " + e.Message.Split('\n')[0];
            _indexProgress.Foreground = Ux.WarnBrush;
        }
        finally
        {
            if (generation == _indexGeneration) _indexGeneration++;
            _indexBuilding = false;
            cancellation.Dispose();
            if (ReferenceEquals(_indexCancellation, cancellation)) _indexCancellation = null;
        }
    }

    internal void CancelIndexOnClose()
    {
        _indexGeneration++;
        _indexCancellation?.Cancel();
    }

    private void ShowIndexProgress(AnimationIndexProgress progress, int generation)
    {
        if (!_indexBuilding || generation != _indexGeneration) return;
        _indexProgress.Text = progress.Total == 0
            ? progress.Phase
            : $"indexing {progress.Done}/{progress.Total} — {progress.Phase}";
    }

    private void ApplyIndex(AnimationIndex index)
    {
        _animationIndex = index;
        ResetIndexFilters(index.Entries);
        ApplyIndexFilter();

        string note = index.CacheNote == null ? "" : "  —  " + index.CacheNote;
        _indexProgress.Text = $"indexed {index.Entries.Count} entries in {index.Elapsed.TotalSeconds:F1}s — " +
                              $"{index.ReusedSources} source(s) reused, {index.ScannedSources} scanned{note}";
        _indexProgress.Foreground = index.CacheNote == null ? Ux.MutedBrush : Ux.WarnBrush;

        SetStatus($"Animation index: {index.Entries.Count} entries across {index.Sources.Count} " +
                  $"source(s) under {Path.GetFileName(index.Request.DataFolder.TrimEnd(Path.DirectorySeparatorChar))}" +
                  (index.Request.ModRootsHighToLow.Count > 0
                      ? $", layered over {index.Request.ModRootsHighToLow.Count} mod root(s)"
                      : ""), Ux.MetaBrush);
    }

    private void ResetIndexFilters(IReadOnlyList<AnimationIndexEntry> entries)
    {
        _indexFilterPaused = true;
        try
        {
            string kind = SelectedText(_indexKind, AllKinds);
            string project = SelectedText(_indexProject, AllProjects);
            string folder = SelectedText(_indexFolder, AllFolders);

            var projects = entries.Where(entry => entry.Project.Length > 0).Select(entry => entry.Project)
                                  .Distinct(StringComparer.OrdinalIgnoreCase)
                                  .Where(name => !name.Equals(AllProjects, StringComparison.OrdinalIgnoreCase))
                                  .OrderBy(name => name, StringComparer.OrdinalIgnoreCase).ToList();
            var folders = entries.Where(entry => entry.Folder.Length > 0).Select(entry => entry.Folder)
                                 .Distinct(StringComparer.OrdinalIgnoreCase)
                                 .Where(name => !name.Equals(AllFolders, StringComparison.OrdinalIgnoreCase))
                                 .OrderBy(name => name, StringComparer.OrdinalIgnoreCase).ToList();

            FillCombo(_indexKind,
                AnimationIndexView.CappedValues(
                    entries.Select(entry => AnimationIndex.Label(entry.Kind)).Distinct(StringComparer.Ordinal)
                           .OrderBy(name => name, StringComparer.Ordinal), AllKinds,
                    AnimationIndexView.MaxFilterValues, out _), kind);
            FillCombo(_indexProject,
                AnimationIndexView.CappedValues(projects, AllProjects, AnimationIndexView.MaxFilterValues, out _),
                project);
            FillCombo(_indexFolder,
                AnimationIndexView.CappedValues(folders, AllFolders, AnimationIndexView.MaxFilterValues, out _),
                folder);
        }
        finally
        {
            _indexFilterPaused = false;
        }
    }

    private static void FillCombo(ComboBox combo, IReadOnlyList<string> items, string keep)
    {
        combo.ItemsSource = items;
        int at = keep.Length == 0 ? -1 : items.ToList().IndexOf(keep);
        combo.SelectedIndex = at < 0 ? 0 : at;
    }

    private static string SelectedText(ComboBox combo, string fallback) =>
        combo.SelectedItem as string ?? fallback;

    private void ApplyIndexFilter()
    {
        if (_indexFilterPaused) return;

        IReadOnlyList<AnimationIndexEntry> all = _animationIndex?.Entries
            ?? (IReadOnlyList<AnimationIndexEntry>)Array.Empty<AnimationIndexEntry>();
        string[] words = (_indexSearch.Text ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        string kind = AnimationIndexView.FilterValue(SelectedText(_indexKind, AllKinds), AllKinds);
        string project = AnimationIndexView.FilterValue(SelectedText(_indexProject, AllProjects), AllProjects);
        string folder = AnimationIndexView.FilterValue(SelectedText(_indexFolder, AllFolders), AllFolders);

        var shown = new List<AnimationIndexEntry>();
        foreach (var entry in all)
        {
            if (kind != AllKinds && AnimationIndex.Label(entry.Kind) != kind) continue;
            if (project != AllProjects && entry.Project != project) continue;
            if (folder != AllFolders && entry.Folder != folder) continue;
            if (!MatchesIndexWords(entry, words)) continue;
            shown.Add(entry);
        }

        _indexShown = shown.ToArray();
        int painted = PaintIndexGrid(_indexShown);

        if (_animationIndex == null)
        {
            _indexSummary.Text = "no index built yet";
            return;
        }

        string count = shown.Count == all.Count
            ? $"{all.Count} entries"
            : $"{shown.Count} of {all.Count} entries";
        _indexSummary.Text = painted < shown.Count ? $"{count}, showing {painted}" : count;
    }

    private static bool MatchesIndexWords(AnimationIndexEntry entry, string[] words)
    {
        if (words.Length == 0) return true;
        string haystack = entry.VirtualPath + "\n" + entry.Source + "\n" + entry.Project + "\n" +
                          entry.Character + "\n" + AnimationIndex.Label(entry.Kind);
        foreach (string word in words)
            if (!haystack.Contains(word, StringComparison.OrdinalIgnoreCase)) return false;
        return true;
    }

    private int PaintIndexGrid(IReadOnlyList<AnimationIndexEntry> shown)
    {
        _indexGrid.Clear();
        var groups = AnimationIndexView.Groups(shown);
        foreach (var group in groups)
        {
            var head = _indexGrid.Add(null, group.Heading, "", "", "")
                                 .Colour(0, Ux.TitleBrush);
            foreach (var entry in group.Entries)
            {
                _indexGrid.Add(head, entry.VirtualPath, entry.Source, AnimationIndex.Label(entry.Kind),
                               entry.Project)
                          .Colour(0, Ux.CodeBrush).Colour(2, Ux.MutedBrush).Tag(entry);
            }
        }
        return AnimationIndexView.ShownCount(groups);
    }

    private void UpdateIndexStatus()
    {
        if (_indexGrid.SelectedTag is not AnimationIndexEntry entry) return;
        string size = entry.Size > 0 ? $"   {entry.Size} bytes" : "";
        string project = entry.Project.Length > 0 ? $"   project {entry.Project}" : "";
        SetStatus($"{entry.VirtualPath}   [{entry.Source}]   {AnimationIndex.Label(entry.Kind)}{project}{size}",
                  Ux.MetaBrush);
    }

    private void OpenSelectedIndexEntry()
    {
        if (_indexGrid.SelectedTag is not AnimationIndexEntry entry)
        {
            SetStatus("Pick an entry in the index first.", Ux.MutedBrush);
            return;
        }

        OpenIndexEntry(entry);
    }

    private string? _pendingVirtualPathHint;

    private bool OpenIndexEntryAt(string path, string virtualPath, string sourceFolder = "")
    {
        _pendingVirtualPathHint = virtualPath;
        _pendingSourceFolder = sourceFolder;
        return Open(path);
    }

    private void OpenIndexEntry(AnimationIndexEntry entry)
    {
        if (entry.Location.Length == 0)
        {
            SetStatus($"{entry.VirtualPath} was listed without a location to open.", Ux.WarnBrush);
            return;
        }

        if (entry.Source.StartsWith("loose:", StringComparison.OrdinalIgnoreCase))
        {
            if (!File.Exists(entry.Location))
            {
                SetStatus($"{entry.VirtualPath} is not there any more: {entry.Location}", Ux.WarnBrush);
                return;
            }

            OpenIndexEntryAt(entry.Location, entry.VirtualPath);
            return;
        }

        AnimationIndex? index = _animationIndex;
        if (index == null)
        {
            SetStatus("Build the index before opening one of its entries.", Ux.MutedBrush);
            return;
        }

        try
        {
            if (entry.Size > 0) InputFilePolicy.EnsureHkx(entry.Size, entry.VirtualPath);

            using GameData data = index.Request.OpenGameData();
            if (!data.TryReadEntry(entry.Location, entry.VirtualPath, out byte[] bytes, out string? warning))
            {
                SetStatus($"{entry.VirtualPath} is no longer inside {Path.GetFileName(entry.Location)}" +
                          (warning == null ? "." : ": " + warning), Ux.WarnBrush);
                return;
            }

            InputFilePolicy.EnsureHkx(bytes.Length, entry.VirtualPath);
            string folder = Path.Combine(Path.GetTempPath(), "BehaviourGraphStudio", TempDirKey(entry.Location));
            Directory.CreateDirectory(folder);
            string copy = Path.Combine(folder, Ba2.FlatFileName(entry.VirtualPath));
            File.WriteAllBytes(copy, bytes);

            if (!OpenIndexEntryAt(copy, entry.VirtualPath, Path.GetDirectoryName(entry.Location) ?? "")) return;
            _readOnly = true;
            _readOnlyWhy = $"{entry.VirtualPath} came out of {Path.GetFileName(entry.Location)}, and " +
                           "nothing here writes back into an archive. Save a copy somewhere of " +
                           "your own and open that to edit it.";
            RefreshSaveAnimationState();
            SetStatus($"Opened {entry.VirtualPath} from {Path.GetFileName(entry.Location)}, read only. " +
                      $"The copy is at {copy}", Ux.MetaBrush);
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException
                                       or ArgumentException)
        {
            SetStatus($"Could not open {Path.GetFileName(entry.VirtualPath)}: " + e.Message.Split('\n')[0],
                      Ux.BadBrush);
        }
    }

    private string DescribeIndexSource(AnimationIndexRequest request)
    {
        string data = Path.GetFileName(request.DataFolder.TrimEnd(Path.DirectorySeparatorChar));
        return request.ModRootsHighToLow.Count == 0
            ? data
            : $"{data} + {request.ModRootsHighToLow.Count} mod root(s)";
    }

    internal HkGrid IndexGrid => _indexGrid;
    internal TextBox IndexSearchField => _indexSearch;
    internal TextBox IndexDataField => _indexDataField;
    internal ComboBox IndexKindFilter => _indexKind;
    internal ComboBox IndexProjectFilter => _indexProject;
    internal ComboBox IndexFolderFilter => _indexFolder;
    internal string IndexSummary => _indexSummary.Text ?? "";
    internal string IndexProgressText => _indexProgress.Text ?? "";
    internal string IndexSourceSummary => _animationIndex == null ? "" : DescribeIndexSource(_animationIndex.Request);
    internal int IndexEntryCount => _animationIndex?.Entries.Count ?? 0;
    internal int IndexShownCount => _indexShown.Length;

    internal void IndexNowForTest(string? cachePath)
    {
        var request = IndexRequestFor(_indexDataField.Text ?? "", out string failure);
        if (request == null)
        {
            _indexProgress.Text = failure;
            _indexProgress.Foreground = Ux.WarnBrush;
            return;
        }

        ApplyIndex(AnimationIndex.Build(request, AnimationIndexOptions.Fresh(cachePath), null, CancellationToken.None));
    }

    internal void OpenIndexSelectionForTest() => OpenSelectedIndexEntry();

    internal bool SelectIndexKindForTest(string label)
    {
        if (_indexKind.ItemsSource is not IEnumerable<string> items) return false;
        int at = items.ToList().IndexOf(label);
        if (at < 0) return false;
        _indexKind.SelectedIndex = at;
        return true;
    }

    internal bool SelectIndexEntryForTest(string virtualPath)
    {
        var entry = _indexShown.FirstOrDefault(candidate =>
            candidate.VirtualPath.Equals(virtualPath, StringComparison.OrdinalIgnoreCase));
        return entry != null && _indexGrid.SelectByTag(entry);
    }
}
