using System;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.ViewModels;
using Microsoft.Web.WebView2.Core;

namespace FO4RecordEditor;

public partial class MainWindow : Wpf.Ui.Controls.FluentWindow
{
    private readonly ShellViewModel _shell = new();
    private readonly AppInterop _appInterop;

    // Lock objects for cross-thread access
    private readonly object _logLock = new();
    private readonly object _errorsLock = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _shell;

        System.Windows.Data.BindingOperations.EnableCollectionSynchronization(_shell.Log.Entries, _logLock);
        System.Windows.Data.BindingOperations.EnableCollectionSynchronization(_shell.Errors, _errorsLock);

        // Forward loader progress to the React UI as web messages (status text + progress bar).
        _appInterop = new AppInterop(_shell, (msg, pct) =>
        {
            if (!string.IsNullOrEmpty(msg)) SetStatus(msg);
            SetProgress(pct ?? -1);
        });

        RegisterCommands();
        InitializeAsync();
        Services.Ai.PluginToolExecutor.ToolCompleted += OnMcpToolCompleted;
    }

    private void OnMcpToolCompleted(Services.Ai.PluginToolExecutor.McpToolEvent ev)
    {
        Dispatcher.BeginInvoke(() =>
        {
            var msg = new
            {
                Type = "McpLive",
                Tool = ev.Tool,
                Plugin = ev.Plugin,
                Record = ev.Record,
                Field = ev.Field,
                Summary = ev.Summary,
                IsWrite = ev.IsWrite,
            };
            AppWebView.CoreWebView2?.PostWebMessageAsJson(
                Newtonsoft.Json.JsonConvert.SerializeObject(msg));
        });
    }

    private async void InitializeAsync()
    {
        var env = await CoreWebView2Environment.CreateAsync(null, System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FO4RecordEditor_Main_WebView2"));
        await AppWebView.EnsureCoreWebView2Async(env);
        
        AppWebView.CoreWebView2.AddHostObjectToScript("appInterop", _appInterop);
        // ConflictResolver.tsx calls hostObjects.backend.* -- this was never registered (the bug).
        AppWebView.CoreWebView2.AddHostObjectToScript("backend", new BackendInterop(_shell));
        // Claude AI panel bridge. The agent's onText/onToolStatus callbacks can fire off the UI
        // thread, so marshal every web-message post through the Dispatcher before PostWebMessageAsJson.
        AppWebView.CoreWebView2.AddHostObjectToScript("chat", new Services.ChatInterop(_shell,
            payload => Dispatcher.BeginInvoke(() =>
                AppWebView.CoreWebView2?.PostWebMessageAsJson(Newtonsoft.Json.JsonConvert.SerializeObject(payload)))));
        // React Settings panel (replaces the WPF SettingsDialog).
        AppWebView.CoreWebView2.AddHostObjectToScript("settings", new Services.SettingsInterop(_shell));

#if DEBUG
        AppWebView.Source = new Uri("http://localhost:5173/#/main");
#else
        var path = System.IO.Path.Combine(System.AppDomain.CurrentDomain.BaseDirectory, "web", "dist");
        AppWebView.CoreWebView2.SetVirtualHostNameToFolderMapping("app.local", path, CoreWebView2HostResourceAccessKind.Allow);
        AppWebView.Source = new Uri("https://app.local/index.html#/main");
#endif
    }

    private async void OpenRecordTab(RecordNode record)
    {
        // For now, let's just log it. React will handle the UI tabs eventually,
        // but if C# needs to open a record, we can send a message to React.
        if (record.GetValue("_HasData") == "False")
        {
            var env = _shell.GameEnvironment;
            var plugin = record.Parent?.Parent?.Key ?? "";
            var fkStr = record.GetValue("FormKey") ?? "";
            try
            {
                var fields = await Task.Run(() =>
                    Services.MutagenLoader.BuildPopulatedFields(fkStr, env, plugin));

                var flag = record.GetChild("_HasData");
                if (flag != null) record.Children.Remove(flag);
                foreach (var child in fields)
                {
                    if (record.GetChild(child.Key) != null) continue;
                    child.Parent = record;
                    record.Children.Add(child);
                }
            }
            catch (Exception ex)
            {
                _shell.Log.Log(LogCategory.App, LogLevel.Error, "Record load failed", ex.Message);
            }
        }
        
        // Notify React to open the tab via WebMessage
        var msg = new { Type = "OpenRecord", FormKey = record.GetValue("FormKey"), EditorID = record.GetValue("EditorID") };
        AppWebView.CoreWebView2.PostWebMessageAsJson(Newtonsoft.Json.JsonConvert.SerializeObject(msg));
    }

    private void RegisterCommands()
    {
        var c = _shell.Commands;
        c.Register("scan.errors", "Run Error Scan", "Errors", async () => await _shell.RunErrorScanAsync());
        c.Register("settings", "Open Settings", "App", () => new Views.SettingsDialog(_shell) { Owner = this }.ShowDialog());
        c.Register("save", "Save Plugin", "File", () => _shell.SaveSelectedPlugin());
        c.Register("undo", "Undo", "Edit", () => _shell.Undo.Undo());
        c.Register("redo", "Redo", "Edit", () => _shell.Undo.Redo());
    }

    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.P && Keyboard.Modifiers == (ModifierKeys.Control | ModifierKeys.Shift))
            { TogglePalette(); e.Handled = true; }
        else if (e.Key == Key.Z && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Undo(); e.Handled = true; }
        else if (e.Key == Key.Y && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.Undo.Redo(); e.Handled = true; }
        else if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.Control)
            { _shell.SaveSelectedPlugin(); e.Handled = true; }
        else if (e.Key == Key.Escape)
            PaletteOverlay.Visibility = Visibility.Collapsed;
    }

    private void TogglePalette()
    {
        if (PaletteOverlay.Visibility == Visibility.Visible)
            { PaletteOverlay.Visibility = Visibility.Collapsed; return; }
        var palette = new Views.CommandPalette(_shell.Commands);
        palette.Dismiss += () => PaletteOverlay.Visibility = Visibility.Collapsed;
        PaletteHost.Content = palette;
        PaletteOverlay.Visibility = Visibility.Visible;
    }

    public void SetStatus(string text)
    {
        // Send status to React
        var msg = new { Type = "SetStatus", Text = text };
        AppWebView.CoreWebView2?.PostWebMessageAsJson(Newtonsoft.Json.JsonConvert.SerializeObject(msg));
    }

    public void SetProgress(double value)
    {
        // Send progress to React
        var msg = new { Type = "SetProgress", Value = value };
        AppWebView.CoreWebView2?.PostWebMessageAsJson(Newtonsoft.Json.JsonConvert.SerializeObject(msg));
    }
}
