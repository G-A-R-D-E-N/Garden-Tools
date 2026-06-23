using System.Collections.Generic;
using System.Windows;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using Microsoft.Web.WebView2.Core;

namespace FO4RecordEditor.Views;

public partial class ConflictView : Window
{
    private readonly IEnumerable<ConflictEntry> _conflicts;
    private readonly object? _env;
    private readonly BackendInterop _interop;

    public ConflictView(IEnumerable<ConflictEntry> conflicts, object? env,
        Action<string> submitToChat, Action<string>? focusInExplorer = null)
    {
        InitializeComponent();
        _conflicts = conflicts;
        _env = env;
        _interop = new BackendInterop(conflicts, env);
        
        InitializeAsync();
    }

    private async void InitializeAsync()
    {
        // Must await initialization before using WebView2
        var env = await CoreWebView2Environment.CreateAsync(null, System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FO4RecordEditor_WebView2"));
        await WebView.EnsureCoreWebView2Async(env);
        
        WebView.CoreWebView2.AddHostObjectToScript("backend", _interop);
        
#if DEBUG
        WebView.Source = new System.Uri("http://localhost:5173/#/");
#else
        var path = System.IO.Path.Combine(System.AppDomain.CurrentDomain.BaseDirectory, "web", "dist");
        WebView.CoreWebView2.SetVirtualHostNameToFolderMapping("app.local", path, CoreWebView2HostResourceAccessKind.Allow);
        WebView.Source = new System.Uri("https://app.local/index.html#/");
#endif
    }
}
