using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.Models;
using FO4RecordEditor.ViewModels;
using Microsoft.Win32;

namespace FO4RecordEditor.Views;

public partial class ExplorerPanel : UserControl
{
    private readonly ShellViewModel _shell;
    private readonly Action<RecordNode> _open;
    private readonly Action<string> _submitToChat;
    public bool StartInSearch { get; set; }

    public ExplorerPanel(ShellViewModel shell, Action<RecordNode> open, Action<string> submitToChat)
    {
        InitializeComponent();
        _shell = shell;
        _open = open;
        _submitToChat = submitToChat;
        Tree.ItemsSource = shell.Plugins;
    }


    private async void OpenEsp_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "FO4 Plugins|*.esp;*.esm;*.esl", Multiselect = true };
        if (dlg.ShowDialog() != true) return;

        var main = Window.GetWindow(this) as MainWindow;
        var progress = new Progress<(string message, double? percent)>(p =>
        {
            if (main != null)
            {
                main.SetStatus(p.message);
                if (p.percent.HasValue) main.SetProgress(p.percent.Value);
                else main.SetProgress(-1);
            }
        });

        foreach (var path in dlg.FileNames) await _shell.LoadEspAsync(path);
        if (main != null) main.SetProgress(-1);
    }

    private async void LoadEnv_Click(object sender, RoutedEventArgs e)
    {
        var main = Window.GetWindow(this) as MainWindow;
        var progress = new Progress<(string message, double? percent)>(p =>
        {
            if (main != null)
            {
                main.SetStatus(p.message);
                if (p.percent.HasValue) main.SetProgress(p.percent.Value);
                else main.SetProgress(-1);
            }
        });

        var plugins = await _shell.LoadEnvironmentAsync(progress);
        if (main != null) main.SetProgress(-1);
        if (plugins.Count == 0)
        {
            MessageBox.Show("No plugins found, or the game environment could not be built. " +
                "Make sure Fallout 4 is detected (run via MO2 to see your modlist).");
            return;
        }

        var dlg = new PluginSelectDialog(plugins) { Owner = main };
        if (dlg.ShowDialog() != true) return;

        // Fresh env load: clear the tree, then add a lazy node per selected plugin.
        _shell.Plugins.Clear();
        foreach (var name in dlg.SelectedPlugins)
            _shell.Plugins.Add(FO4RecordEditor.Services.MutagenLoader.MakeLazyNode(name));
        main?.SetStatus($"Loaded {dlg.SelectedPlugins.Count} plugin(s).");
    }

    private async void OpenMo2_Click(object sender, RoutedEventArgs e)
    {
        var main = Window.GetWindow(this) as MainWindow;

        var dlg = new Microsoft.Win32.OpenFolderDialog
        {
            Title = "Select your Mod Organizer 2 instance folder (the one containing 'mods' and 'profiles')",
        };
        var last = _shell.Settings.Current.Mo2InstancePath;
        if (!string.IsNullOrWhiteSpace(last) && System.IO.Directory.Exists(last)) dlg.InitialDirectory = last;
        if (dlg.ShowDialog() != true) return;

        var instance = dlg.FolderName;
        if (!System.IO.Directory.Exists(System.IO.Path.Combine(instance, "profiles")))
        {
            MessageBox.Show("That folder doesn't look like an MO2 instance — it has no 'profiles' subfolder. " +
                "Pick the instance folder that contains 'mods', 'profiles', and 'ModOrganizer.ini'.");
            return;
        }

        var progress = new Progress<(string message, double? percent)>(p =>
        {
            if (main != null)
            {
                main.SetStatus(p.message);
                if (p.percent.HasValue) main.SetProgress(p.percent.Value); else main.SetProgress(-1);
            }
        });

        var plugins = await _shell.LoadMo2ProfileAsync(instance, progress);
        if (main != null) main.SetProgress(-1);
        if (plugins.Count == 0)
        {
            MessageBox.Show("No plugins loaded from that MO2 profile. Check that the profile's plugins.txt exists " +
                "and the game Data folder is set in ModOrganizer.ini.");
            return;
        }

        var pick = new PluginSelectDialog(plugins) { Owner = main };
        if (pick.ShowDialog() != true) return;

        _shell.Plugins.Clear();
        foreach (var name in pick.SelectedPlugins)
            _shell.Plugins.Add(FO4RecordEditor.Services.MutagenLoader.MakeLazyNode(name));
        main?.SetStatus($"Loaded {pick.SelectedPlugins.Count} plugin(s) from MO2.");
    }

    private async void Scan_Click(object sender, RoutedEventArgs e) => await _shell.RunErrorScanAsync();

    private void Tree_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        // Only actual records (nodes with a FormKey) open in a center tab. Group/plugin
        // nodes are left to the TreeView's default double-click expand/collapse, so we no
        // longer open a group's lazy "Loading..." placeholder as a record.
        if (Tree.SelectedItem is RecordNode n && n.GetValue("FormKey") != null)
            _open(n);
    }

    private void Tree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is RecordNode n) _shell.SelectedNode = n;
    }

    private async void Tree_Expanded(object sender, RoutedEventArgs e)
    {
        try
        {
            if (e.OriginalSource is TreeViewItem tvi && tvi.Header is RecordNode node)
            {
                var dummy = node.GetChild("Loading...");
                if (dummy == null) return;

                var needsGroups = dummy.GetValue("_NeedsGroups");
                var needsRecords = dummy.GetValue("_NeedsRecords");
                
                if (needsGroups != null)
                {
                    var main = Window.GetWindow(this) as MainWindow;
                    if (main != null) main.SetProgress(0); // Indeterminate start

                    dummy.Values[""] = "Loading Groups...";
                    var env = _shell.GameEnvironment;
                    var newNodes = await Task.Run(() => 
                    {
                        return FO4RecordEditor.Services.MutagenLoader.GetGroups(needsGroups, env, node, 
                            p => Application.Current.Dispatcher.BeginInvoke(() => {
                                dummy.Values[""] = p;
                                if (main != null) main.SetStatus($"{needsGroups}: {p}");
                            }));
                    });
                    node.Children.Remove(dummy);
                    foreach (var n in newNodes) node.Children.Add(n);
                    if (main != null) main.SetProgress(-1);
                }
                else if (needsRecords != null)
                {
                    var main = Window.GetWindow(this) as MainWindow;
                    if (main != null) main.SetProgress(0); // Indeterminate start

                    dummy.Values[""] = "Loading Records...";
                    var parts = needsRecords.Split('|');
                    if (parts.Length == 2)
                    {
                        var env = _shell.GameEnvironment;
                        var newNodes = await Task.Run(() => 
                        {
                            return FO4RecordEditor.Services.MutagenLoader.GetRecords(parts[0], parts[1], env, node,
                                p => Application.Current.Dispatcher.BeginInvoke(() => {
                                    dummy.Values[""] = p;
                                    if (main != null) main.SetStatus($"{parts[0]} [{parts[1]}]: {p}");
                                }));
                        });
                        node.Children.Remove(dummy);
                        foreach (var n in newNodes) node.Children.Add(n);
                    }
                    if (main != null) main.SetProgress(-1);
                }
            }
        }
        catch (Exception ex)
        {
            _shell.Log.Log(FO4RecordEditor.Models.LogCategory.App, FO4RecordEditor.Models.LogLevel.Error, $"Error expanding tree: {ex.Message}", ex.StackTrace);
        }
    }

    private void Search_TextChanged(object sender, TextChangedEventArgs e)
    {
        var q = SearchBox.Text.Trim();
        if (q.Length < 2) return;
        var hit = _shell.Plugins.SelectMany(p => p.SelfAndDescendants())
            .FirstOrDefault(n => n.Key.Contains(q, StringComparison.OrdinalIgnoreCase)
                              || n.Value.Contains(q, StringComparison.OrdinalIgnoreCase));
        if (hit != null) { var p = hit.Parent; while (p != null) { p.IsExpanded = true; p = p.Parent; } hit.IsSelected = true; }
    }

    private void Tree_ContextMenuOpening(object sender, ContextMenuEventArgs e)
    {
        if (Tree.SelectedItem is not RecordNode n) { e.Handled = true; return; }
        if (IsPluginNode(n)) { Tree.ContextMenu = BuildPluginMenu(n.Key); return; }
        if (n.GetValue("FormKey") != null) { Tree.ContextMenu = AiActions.Build(_shell, n, _open, _submitToChat); return; }
        e.Handled = true;
    }

    private static bool IsPluginNode(RecordNode n) =>
        n.Key.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) ||
        n.Key.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) ||
        n.Key.EndsWith(".esl", StringComparison.OrdinalIgnoreCase);

    private ContextMenu BuildPluginMenu(string plugin)
    {
        var menu = new ContextMenu();
        AddItem(menu, "Check for Errors (crash refs)", () => RunScan(plugin, "errors"));
        AddItem(menu, "Conflict Checker (all plugins)", RunConflictScan);
        menu.Items.Add(new Separator());
        AddItem(menu, "Clean (undelete + disable)", () => RunMaintain(plugin, "clean"));
        AddItem(menu, "Compact to ESL", () => RunMaintain(plugin, "compact"));
        return menu;
    }

    private static void AddItem(ContextMenu menu, string header, Action action)
    {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        menu.Items.Add(item);
    }

    // The plugin file name for the selected node (itself or nearest plugin ancestor).
    private string? SelectedPlugin()
    {
        var n = Tree.SelectedItem as RecordNode;
        while (n != null) { if (IsPluginNode(n)) return n.Key; n = n.Parent; }
        return null;
    }

    private async void RunScan(string plugin, string kind)
    {
        var main = Window.GetWindow(this) as MainWindow;
        main?.SetStatus($"Scanning {plugin}...");
        var env = _shell.GameEnvironment;
        var report = await System.Threading.Tasks.Task.Run(() =>
            kind == "errors"
                ? FO4RecordEditor.Services.MutagenLoader.ScanErrorsDeep(env, plugin)
                : FO4RecordEditor.Services.MutagenLoader.ScanConflicts(env, plugin));
        main?.SetStatus("Ready");
        ResultsDialog.Show(main, kind == "errors" ? "Error Scan" : "Conflict Scan", report);
    }

    private async void RunMaintain(string plugin, string kind)
    {
        var main = Window.GetWindow(this) as MainWindow;
        var env = _shell.GameEnvironment;
        var report = await System.Threading.Tasks.Task.Run(() =>
            kind == "clean"
                ? FO4RecordEditor.Services.WriteService.CleanPlugin(plugin, env)
                : FO4RecordEditor.Services.WriteService.CompactToEsl(plugin, env));
        ResultsDialog.Show(main, kind == "clean" ? "Clean Plugin" : "Compact to ESL", report);

        var r = MessageBox.Show($"Save these changes to {plugin}?", "Save changes",
            MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (r == MessageBoxResult.Yes)
        {
            var saveMsg = await System.Threading.Tasks.Task.Run(() =>
                FO4RecordEditor.Services.WriteService.SavePlugin(plugin, null));
            main?.SetStatus(saveMsg);
            ResultsDialog.Show(main, "Save", saveMsg);
        }
    }

    private void ScanErrorsBtn_Click(object sender, RoutedEventArgs e)
    {
        var p = SelectedPlugin();
        if (p == null) { MessageBox.Show("Select a plugin in the tree first."); return; }
        RunScan(p, "errors");
    }

    private void ScanConflictsBtn_Click(object sender, RoutedEventArgs e) => RunConflictScan();

    // Shared across all ExplorerPanel instances so re-clicking (or right-click + toolbar)
    // can't launch a second scan or a second window.
    private static bool _conflictScanRunning;
    private static ConflictView? _conflictWindow;

    // Whole-load-order conflict scan -> modern Conflict Checker grid.
    private async void RunConflictScan()
    {
        var main = Window.GetWindow(this) as MainWindow;
        var env = _shell.GameEnvironment;
        if (env == null)
        {
            MessageBox.Show("Build the environment first with 'Load Env' (run via MO2 to see your modlist).");
            return;
        }
        if (_conflictScanRunning)
        {
            main?.SetStatus("Conflict scan already running...");
            return;
        }

        _conflictScanRunning = true;
        try
        {
            main?.SetStatus("Scanning all plugins for conflicts...");
            var progress = new Action<string>(m => Dispatcher.BeginInvoke(() => main?.SetStatus(m)));
            var conflicts = await FO4RecordEditor.Services.ConflictScanner.ScanAsync(env, progress);
            main?.SetStatus($"Found {conflicts.Count} conflict(s). Expand a plugin to see the colours.");

            // Colour the ESP tree (winner = orange, loser = red), like xEdit.
            FO4RecordEditor.Services.ConflictState.Set(conflicts);
            RecolorConflicts();

            // Reuse a single window instead of stacking new ones.
            if (_conflictWindow != null) { try { _conflictWindow.Close(); } catch { } }
            _conflictWindow = new ConflictView(conflicts, env, _submitToChat,
                s => { SearchBox.Text = s; SearchBox.Focus(); }) { Owner = main };
            _conflictWindow.Closed += (_, _) => _conflictWindow = null;
            _conflictWindow.Show();
        }
        finally
        {
            _conflictScanRunning = false;
        }
    }

    // Apply the latest conflict scan colours: plugin nodes (always visible) and any
    // loaded record nodes. Newly-expanded records are coloured by GetRecords.
    private void RecolorConflicts()
    {
        foreach (var root in _shell.Plugins)
            foreach (var n in root.SelfAndDescendants())
            {
                if (n.IsRecordNode)
                {
                    var plugin = n.Parent?.Parent?.Key ?? "";
                    var fk = n.GetValue("FormKey") ?? "";
                    n.ConflictStatus = FO4RecordEditor.Services.ConflictState.GetStatus(plugin, fk);
                }
                else if (IsPluginNode(n))
                {
                    n.ConflictStatus = FO4RecordEditor.Services.ConflictState.GetPluginStatus(n.Key);
                }
            }
    }
}
