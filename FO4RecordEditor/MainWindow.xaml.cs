using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.ViewModels;
using Microsoft.Win32;

namespace FO4RecordEditor;

public partial class MainWindow : Window
{
    public ObservableCollection<RecordNode> RootNodes { get; } = [];
    private ScriptContext? _scriptCtx;
    private bool _scriptVisible;
    private RecordNode? _selectedNode;

    // Breadcrumb navigation stack (for drilling into compound fields)
    private readonly Stack<RecordNode> _navStack = new();

    private static readonly Dictionary<string, string> _templates = new()
    {
        ["Find NULL GetBaseValue"] = """
            var hits = Ctx.WithKey("ActorValue")
                          .Where(n => n.Value.Contains("NULL") || n.Value.Contains("000000"))
                          .ToList();
            Log($"NULL ActorValue conditions: {hits.Count}");
            foreach (var h in hits.Take(20))
                Log("  " + Ctx.Navigate(h, "../../..EditorID")?.Value + "  " + h.Value);
            """,

        ["List all EditorIDs"] = """
            var ids = Ctx.WithKey("EditorID")
                         .Where(n => !string.IsNullOrEmpty(n.Value))
                         .Select(n => n.Value)
                         .OrderBy(v => v)
                         .ToList();
            Log($"Total: {ids.Count}");
            foreach (var id in ids.Take(100)) Log("  " + id);
            if (ids.Count > 100) Log($"  ... +{ids.Count - 100} more");
            """,

        ["Count records by type"] = """
            var groups = Ctx.All
                .Where(n => n.GetValue("Type") != null)
                .GroupBy(n => n.GetValue("Type")!)
                .OrderByDescending(g => g.Count());
            foreach (var g in groups)
                Log($"  {g.Key,-20} {g.Count(),6}");
            """,

        ["COBJ with NULL ActorValue"] = """
            int count = 0;
            foreach (var rec in Ctx.RecordsOfType("COBJ"))
            {
                var bad = rec.Descendants()
                    .Where(n => n.Key == "ActorValue" && (n.Value.Contains("NULL") || n.Value == ""))
                    .ToList();
                if (bad.Any())
                {
                    Log(rec.Key + " -> " + bad.Count + " bad condition(s)");
                    count++;
                }
            }
            Log($"\nTotal COBJ with NULL ActorValue: {count}");
            """,

        ["Bulk set field"] = """
            // Example: set a field value across all matching records
            int changed = 0;
            foreach (var n in Ctx.WithKey("YourFieldName"))
            {
                n.Value = "NewValue";
                changed++;
            }
            Log($"Changed {changed} records. Run Save All to persist.");
            """,

        ["Find by FormKey"] = """
            var formKey = "FE149165"; // change this
            var hits = Ctx.All.Where(n => n.Value.Contains(formKey)).ToList();
            Log($"References to {formKey}: {hits.Count}");
            foreach (var h in hits.Take(20))
                Log("  " + h.Key + " = " + h.Value + "  (in " + Path.GetFileName(h.FilePath ?? "") + ")");
            """,
    };

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;
        RecordTree.ItemsSource = RootNodes;
        ScriptEditor.Text = _templates["COBJ with NULL ActorValue"];
        UpdateStatus();
    }

    // =====================================================================
    // Toolbar
    // =====================================================================

    private async void OpenEsp_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "FO4 Plugins|*.esp;*.esm;*.esl|All files|*.*",
            Title = "Open ESP/ESM/ESL plugin",
            Multiselect = true
        };
        if (dialog.ShowDialog() != true) return;
        foreach (var path in dialog.FileNames)
            await LoadEspAsync(path);
    }

    private async void OpenFolder_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Select Spriggit serialized folder" };
        if (dialog.ShowDialog() != true) return;
        await LoadFolderAsync(dialog.FolderName);
    }

    private async void OpenFile_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Spriggit files|*.yaml;*.yml;*.json|All files|*.*",
            Title = "Open Spriggit YAML/JSON file",
            Multiselect = true
        };
        if (dialog.ShowDialog() != true) return;
        foreach (var path in dialog.FileNames)
            await LoadYamlFileAsync(path);
    }

    // ---- async loaders --------------------------------------------------

    private async Task LoadEspAsync(string path)
    {
        SetStatus($"Loading {Path.GetFileName(path)}...");
        SetBusy(true);
        try
        {
            var progress = new Progress<string>(msg => SetStatus(msg));
            var node = await Task.Run(() => MutagenLoader.LoadEsp(path, progress));
            RootNodes.Add(node);
            node.IsExpanded = true;
            RefreshScriptContext();
            UpdateStatus();
            SetStatus($"Loaded: {Path.GetFileName(path)}");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to load ESP:\n{ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            SetStatus("Load failed.");
        }
        finally { SetBusy(false); }
    }

    private async Task LoadFolderAsync(string path)
    {
        SetStatus($"Loading {path}...");
        SetBusy(true);
        try
        {
            var node = await Task.Run(() => SpriggitLoader.LoadFolder(path));
            RootNodes.Add(node);
            node.IsExpanded = true;
            RefreshScriptContext();
            UpdateStatus();
            SetStatus($"Loaded: {path}");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to load folder:\n{ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            SetStatus("Load failed.");
        }
        finally { SetBusy(false); }
    }

    private async Task LoadYamlFileAsync(string path)
    {
        SetStatus($"Loading {Path.GetFileName(path)}...");
        SetBusy(true);
        try
        {
            var node = await Task.Run(() => SpriggitLoader.LoadFile(path));
            RootNodes.Add(node);
            RefreshScriptContext();
            UpdateStatus();
            SetStatus($"Loaded: {Path.GetFileName(path)}");
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to load file:\n{ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally { SetBusy(false); }
    }

    // ---- save / close ---------------------------------------------------

    private void SaveAll_Click(object sender, RoutedEventArgs e)
    {
        int saved = 0;
        foreach (var root in RootNodes)
            saved += SaveDirtyFiles(root);
        SetStatus($"Saved {saved} file(s).");
        UpdateStatus();
    }

    private int SaveDirtyFiles(RecordNode node)
    {
        int count = 0;
        if (node.AnyDirty && !node.IsLeaf && node.FilePath != null && File.Exists(node.FilePath))
        {
            SpriggitLoader.SaveFile(node);
            count++;
        }
        foreach (var child in node.Children)
            count += SaveDirtyFiles(child);
        return count;
    }

    private void SaveFile_Click(object sender, RoutedEventArgs e)
    {
        var fileNode = FindFileAncestor(_selectedNode);
        if (fileNode == null) return;
        SpriggitLoader.SaveFile(fileNode);
        SetStatus($"Saved: {Path.GetFileName(fileNode.FilePath ?? "")}");
        UpdateStatus();
    }

    private void CloseAll_Click(object sender, RoutedEventArgs e)
    {
        if (RootNodes.Any(n => n.AnyDirty))
        {
            var r = MessageBox.Show("Unsaved changes exist. Close anyway?", "Unsaved",
                MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (r != MessageBoxResult.Yes) return;
        }
        RootNodes.Clear();
        ClearEditor();
        RefreshScriptContext();
        UpdateStatus();
        SetStatus("Closed all.");
    }

    private async void Compile_Click(object sender, RoutedEventArgs e)
    {
        var spriggitDlg = new OpenFileDialog
        {
            Filter = "Spriggit exe|Spriggit*.exe;spriggit*.exe|All executables|*.exe",
            Title = "Select Spriggit.exe (the create-plugin binary)"
        };
        if (spriggitDlg.ShowDialog() != true) return;

        var srcDlg = new OpenFolderDialog { Title = "Select serialized source folder" };
        if (srcDlg.ShowDialog() != true) return;

        var outDlg = new SaveFileDialog
        {
            Filter = "ESP|*.esp|ESM|*.esm|All|*.*",
            DefaultExt = ".esp", Title = "Save compiled ESP as"
        };
        if (outDlg.ShowDialog() != true) return;

        SetStatus("Compiling...");
        ShowScriptPanel();
        var progress = new Progress<string>(msg => AppendOutput(msg + "\n"));
        var result = await CompileService.CompileAsync(
            spriggitDlg.FileName, srcDlg.FolderName, outDlg.FileName, progress);
        AppendOutput(result.Success
            ? $"\n✓ Compiled -> {outDlg.FileName}\n"
            : $"\n✗ Failed (exit {result.ExitCode})\n");
        SetStatus(result.Success ? "Compile succeeded." : "Compile FAILED.");
    }

    private void ExpandAll_Click(object sender, RoutedEventArgs e)   => SetExpandedAll(true);
    private void CollapseAll_Click(object sender, RoutedEventArgs e) => SetExpandedAll(false);
    private void SetExpandedAll(bool v) { foreach (var n in RootNodes) SetExpanded(n, v); }
    private static void SetExpanded(RecordNode n, bool v) { n.IsExpanded = v; foreach (var c in n.Children) SetExpanded(c, v); }

    // =====================================================================
    // Field editor  (shallow -- only direct children, click to drill in)
    // =====================================================================

    private void RecordTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is not RecordNode node) return;
        _navStack.Clear();
        ShowNodeInEditor(node);
    }

    private void ShowNodeInEditor(RecordNode node)
    {
        _selectedNode = node;

        // Flat list of direct children only -- fast even for huge records
        var rows = node.Children
            .Select(c => new FieldRow(c, 0))
            .ToList();

        FieldList.ItemsSource = rows;
        BreadcrumbLabel.Text = BuildBreadcrumb(node);
        NavBackBtn.Visibility = _navStack.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

        var fileNode = FindFileAncestor(node);
        SaveFileBtn.Visibility = fileNode?.FilePath != null && File.Exists(fileNode.FilePath)
            ? Visibility.Visible : Visibility.Collapsed;
    }

    // Double-click a compound field to drill into it
    private void FieldList_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if ((FieldList.ContainerFromElement(e.OriginalSource as DependencyObject) is not ContentPresenter cp)
            || cp.Content is not FieldRow row) return;
        var node = GetNodeFromRow(row);
        if (node == null || node.IsLeaf) return;
        _navStack.Push(_selectedNode!);
        ShowNodeInEditor(node);
    }

    private void NavBack_Click(object sender, RoutedEventArgs e)
    {
        if (_navStack.Count == 0) return;
        ShowNodeInEditor(_navStack.Pop());
    }

    private static RecordNode? GetNodeFromRow(FieldRow row)
    {
        // FieldRow wraps _node via reflection to avoid exposing it publicly
        var field = typeof(FieldRow).GetField("_node",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        return field?.GetValue(row) as RecordNode;
    }

    private static string BuildBreadcrumb(RecordNode node)
    {
        var parts = new List<string>();
        var cur = node;
        while (cur != null) { parts.Add(cur.Key); cur = cur.Parent; }
        parts.Reverse();
        return string.Join(" > ", parts);
    }

    private static RecordNode? FindFileAncestor(RecordNode? node)
    {
        var cur = node;
        while (cur != null)
        {
            if (cur.FilePath != null && File.Exists(cur.FilePath)) return cur;
            cur = cur.Parent;
        }
        return null;
    }

    private void ClearEditor()
    {
        FieldList.ItemsSource = null;
        BreadcrumbLabel.Text = "Select a record in the tree";
        SaveFileBtn.Visibility = Visibility.Collapsed;
        _selectedNode = null;
        _navStack.Clear();
    }

    // =====================================================================
    // Search
    // =====================================================================

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e) => ApplySearch();
    private void ClearSearch_Click(object sender, RoutedEventArgs e) => SearchBox.Clear();

    private void ApplySearch()
    {
        var q = SearchBox.Text.Trim();
        if (string.IsNullOrEmpty(q)) { SearchCountLabel.Text = ""; return; }

        var results = RootNodes
            .SelectMany(r => r.SelfAndDescendants())
            .Where(n => n.Key.Contains(q, StringComparison.OrdinalIgnoreCase) ||
                        n.Value.Contains(q, StringComparison.OrdinalIgnoreCase))
            .ToList();

        foreach (var hit in results) ExpandParents(hit);
        SearchCountLabel.Text = $"{results.Count} match{(results.Count != 1 ? "es" : "")}";

        if (results.Count > 0)
        {
            results[0].IsSelected = true;
            ShowNodeInEditor(results[0]);
        }
    }

    private static void ExpandParents(RecordNode n)
    {
        var p = n.Parent;
        while (p != null) { p.IsExpanded = true; p = p.Parent; }
    }

    // =====================================================================
    // Script panel
    // =====================================================================

    private void ScriptToggle_Click(object sender, MouseButtonEventArgs e) => ToggleScriptPanel();
    private void ToggleScriptPanel() => ShowScriptPanel(!_scriptVisible);

    private void ShowScriptPanel(bool show = true)
    {
        _scriptVisible = show;
        ScriptRow.Height = show ? new GridLength(280) : new GridLength(0);
        ScriptToggleLabel.Text = show ? "[hide]" : "[show]";
    }

    private void ScriptEditor_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && Keyboard.Modifiers == ModifierKeys.Control)
        { RunScript_Click(sender, e); e.Handled = true; }
    }

    private async void RunScript_Click(object sender, RoutedEventArgs e)
    {
        if (_scriptCtx == null) RefreshScriptContext();
        var code = ScriptEditor.Text;
        if (string.IsNullOrWhiteSpace(code)) return;

        ScriptStatusLabel.Text = "Running...";
        ScriptStatusLabel.Foreground = System.Windows.Media.Brushes.Yellow;

        var result = await ScriptEngine.RunAsync(code, _scriptCtx!);
        var output = result.Output + (result.Error != null ? "\n--- ERROR ---\n" + result.Error : "");
        AppendOutput($"--- Run ({result.ElapsedMs}ms) ---\n{output}\n");

        ScriptStatusLabel.Text = result.Success ? $"OK  {result.ElapsedMs}ms" : "Error";
        ScriptStatusLabel.Foreground = result.Success
            ? System.Windows.Media.Brushes.LightGreen
            : System.Windows.Media.Brushes.Tomato;

        UpdateStatus();
    }

    private void ClearScript_Click(object sender, RoutedEventArgs e)  => ScriptEditor.Text = "";
    private void ClearOutput_Click(object sender, RoutedEventArgs e)  => ScriptOutput.Text = "";

    private void LoadScriptTemplate_Click(object sender, RoutedEventArgs e)
    {
        var menu = new ContextMenu();
        foreach (var (name, code) in _templates)
        {
            var item = new MenuItem { Header = name };
            var capture = code;
            item.Click += (_, _) => ScriptEditor.Text = capture;
            menu.Items.Add(item);
        }
        menu.IsOpen = true;
    }

    private void SaveScript_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new SaveFileDialog { Filter = "C# script|*.cs", DefaultExt = ".cs", FileName = "script" };
        if (dlg.ShowDialog() != true) return;
        File.WriteAllText(dlg.FileName, ScriptEditor.Text);
        SetStatus($"Script saved: {Path.GetFileName(dlg.FileName)}");
    }

    private void LoadScript_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Filter = "C# script|*.cs|All files|*.*" };
        if (dlg.ShowDialog() != true) return;
        ScriptEditor.Text = File.ReadAllText(dlg.FileName);
        SetStatus($"Script loaded: {Path.GetFileName(dlg.FileName)}");
    }

    private void AppendOutput(string text)
    {
        ScriptOutput.Text += text;
        ScriptOutput.ScrollToEnd();
        ShowScriptPanel(true);
    }

    // =====================================================================
    // Keyboard shortcuts
    // =====================================================================

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.O      && Keyboard.Modifiers == ModifierKeys.Control) { OpenFolder_Click(sender, e); e.Handled = true; }
        else if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.Control) { SaveAll_Click(sender, e);    e.Handled = true; }
        else if (e.Key == Key.F && Keyboard.Modifiers == ModifierKeys.Control) { SearchBox.Focus();           e.Handled = true; }
        else if (e.Key == Key.OemTilde && Keyboard.Modifiers == ModifierKeys.Control) { ToggleScriptPanel();  e.Handled = true; }
        else if (e.Key == Key.Back && _navStack.Count > 0)                     { NavBack_Click(sender, e);    e.Handled = true; }
    }

    // =====================================================================
    // Helpers
    // =====================================================================

    private void RefreshScriptContext() => _scriptCtx = new ScriptContext(RootNodes);

    private void UpdateStatus()
    {
        var total = RootNodes.SelectMany(r => r.SelfAndDescendants()).Count();
        RecordCountLabel.Text = $"{total:N0} nodes";
        var dirty = RootNodes.Any(n => n.AnyDirty);
        DirtyLabel.Text = dirty ? "● Unsaved changes" : "";
        SaveAllBtn.IsEnabled = dirty;
    }

    private void SetStatus(string msg) => Dispatcher.InvokeAsync(() => StatusLabel.Text = msg);
    private void SetBusy(bool busy)    => Dispatcher.InvokeAsync(() => IsEnabled = !busy);
}
