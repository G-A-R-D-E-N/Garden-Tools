using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace BehaviourStudio.App;

public sealed class AssistantSettingsState
{
    public string Description { get; set; } = "";
    public string SelectedBackend { get; set; } = "codex";
    public string Account { get; set; } = "";
    public string DeviceCodeText { get; set; } = "";
    public bool CodexManaged { get; set; } = true;
    public bool SignedIn { get; set; }
    public IReadOnlyList<CodexModel> Models { get; set; } = Array.Empty<CodexModel>();
    public string SelectedModel { get; set; } = "";
    public string DefaultModelLabel { get; set; } = "";
    public Action<string> ChooseProvider { get; set; } = _ => { };
    public Action<string> ChooseModel { get; set; } = _ => { };
    public Action ResetModel { get; set; } = () => { };
    public Action SignIn { get; set; } = () => { };
    public Action DeviceCode { get; set; } = () => { };
    public Action SignOut { get; set; } = () => { };
}

public sealed class AssistantSettingsWindow : Window
{
    public const string CodexNotice =
        "ChatGPT sign-in happens in your browser or with a device code. BGS never asks for or stores your " +
        "ChatGPT password, OAuth token, or OpenAI API key. Opening this drawer makes no model request. " +
        "Prompts send only bounded editor context and BGS tool results when you press Send.";

    public const string CliNotice =
        "This provider signs in through its own command-line tool, so BGS never sees or stores a credential. " +
        "Opening this drawer makes no model request. Prompts send only bounded editor context and BGS tool " +
        "results when you press Send.";

    private readonly AssistantSettingsState _state;
    private readonly ComboBox _provider = new() { MinWidth = 200 };
    private readonly ComboBox _model = new() { MinWidth = 200 };
    private readonly Button _modelReset = Ux.Secondary("Use provider default");
    private readonly TextBlock _providerText = new();
    private readonly TextBlock _accountText = new();
    private readonly TextBlock _deviceText = new();
    private readonly TextBlock _notice = new();
    private readonly StackPanel _authRow = new() { Orientation = Orientation.Horizontal, Spacing = 8 };
    private readonly Button _signIn = Ux.Primary("Sign in with ChatGPT");
    private readonly Button _deviceCode = Ux.Secondary("Use device code");
    private readonly Button _signOut = Ux.Secondary("Sign out");
    private bool _binding;

    public AssistantSettingsWindow(AssistantSettingsState state)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));

        Title = "Assistant settings";
        Width = 560;
        Height = 430;
        MinWidth = 460;
        MinHeight = 360;
        Background = Ux.BaseBrush;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        CanResize = true;
        ShowActivated = true;

        _provider.SelectionChanged += (_, _) =>
        {
            if (_binding) return;
            if (_provider.SelectedItem is AssistantProviderItem item) _state.ChooseProvider(item.Wire);
        };
        _model.SelectionChanged += (_, _) =>
        {
            if (_binding) return;
            if (_model.SelectedItem is AssistantModelItem item) _state.ChooseModel(item.WireModel);
        };
        _modelReset.Click += (_, _) => _state.ResetModel();
        _signIn.Click += (_, _) => _state.SignIn();
        _deviceCode.Click += (_, _) => _state.DeviceCode();
        _signOut.Click += (_, _) => _state.SignOut();

        var close = Ux.Secondary("Close");
        close.Click += (_, _) => Close();

        _providerText.Foreground = Ux.TitleBrush;
        _providerText.FontSize = Ux.FontSmall;
        _providerText.TextWrapping = TextWrapping.Wrap;
        _accountText.Foreground = Ux.MetaBrush;
        _accountText.FontSize = Ux.FontSmall;
        _accountText.TextWrapping = TextWrapping.Wrap;
        _notice.Foreground = Ux.MutedBrush;
        _notice.FontSize = Ux.FontSmall;
        _notice.TextWrapping = TextWrapping.Wrap;
        _deviceText.Foreground = Ux.CodeBrush;
        _deviceText.FontSize = Ux.FontSmall;
        _deviceText.TextWrapping = TextWrapping.Wrap;

        _authRow.Children.Add(_signIn);
        _authRow.Children.Add(_deviceCode);
        _authRow.Children.Add(_signOut);

        var modelRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            Children = { _model, _modelReset },
        };

        Content = new ScrollViewer
        {
            Content = new StackPanel
            {
                Margin = new Thickness(20),
                Spacing = 10,
                Children =
                {
                    new TextBlock
                    {
                        Text = "Assistant settings",
                        Foreground = Ux.TitleBrush,
                        FontSize = 16,
                    },
                    Section("Provider", _provider),
                    _providerText,
                    _accountText,
                    _deviceText,
                    _authRow,
                    Section("Model", modelRow),
                    _notice,
                    new StackPanel
                    {
                        Orientation = Orientation.Horizontal,
                        HorizontalAlignment = HorizontalAlignment.Right,
                        Margin = new Thickness(0, 6, 0, 0),
                        Children = { close },
                    },
                },
            },
        };

        Refresh();
    }

    public ComboBox ProviderSelector => _provider;
    public string? SelectedProvider => (_provider.SelectedItem as AssistantProviderItem)?.Wire;
    public ComboBox ModelSelector => _model;
    public bool SignInVisible => _signIn.IsVisible;
    public bool DeviceCodeVisible => _deviceCode.IsVisible;
    public bool SignOutVisible => _signOut.IsVisible;
    public string AccountText => _accountText.Text ?? "";
    public string NoticeText => _notice.Text ?? "";
    public string ProviderText => _providerText.Text ?? "";
    public void CloseForTest() => Close();

    public void Present(Window owner)
    {
        if (!IsVisible) Show(owner);
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
        Focus();
    }

    public void Refresh()
    {
        _binding = true;
        try
        {
            var providers = new List<AssistantProviderItem>(AssistantCliLocator.All.Length);
            AssistantProviderItem? current = null;
            foreach (AssistantBackend backend in AssistantCliLocator.All)
                providers.Add(new AssistantProviderItem(
                    AssistantCliLocator.CommandName(backend), AssistantCliLocator.DisplayName(backend)));
            _provider.ItemsSource = providers;
            foreach (AssistantProviderItem item in providers)
                if (item.Wire == _state.SelectedBackend) current = item;
            _provider.SelectedItem = current;

            var models = new List<AssistantModelItem>(_state.Models.Count);
            AssistantModelItem? chosen = null;
            foreach (CodexModel model in _state.Models)
            {
                var item = new AssistantModelItem(model.WireModel, model.DisplayName);
                models.Add(item);
                if (item.WireModel == _state.SelectedModel) chosen = item;
            }
            _model.ItemsSource = models;
            _model.PlaceholderText = _state.DefaultModelLabel;
            _model.SelectedItem = chosen;

            _providerText.Text = _state.Description;
            _accountText.Text = _state.Account;
            _deviceText.Text = _state.DeviceCodeText;
            _deviceText.IsVisible = _state.DeviceCodeText.Length > 0;
            _notice.Text = _state.CodexManaged ? CodexNotice : CliNotice;

            _signIn.IsVisible = _state.CodexManaged && !_state.SignedIn;
            _deviceCode.IsVisible = _state.CodexManaged && !_state.SignedIn;
            _signOut.IsVisible = _state.CodexManaged && _state.SignedIn;
            _modelReset.IsVisible = _state.SelectedModel.Length > 0;
            _model.IsEnabled = _state.Models.Count > 0;
        }
        finally
        {
            _binding = false;
        }
    }

    private static StackPanel Section(string label, Control control) => new()
    {
        Spacing = 4,
        Children =
        {
            new TextBlock { Text = label, Foreground = Ux.MetaBrush, FontSize = Ux.FontSmall },
            control,
        },
    };
}
