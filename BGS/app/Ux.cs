using System.Collections.Generic;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace BehaviourStudio.App;

public static class Ux
{
    public const double FontTitle = 14;
    public const double FontBody = 13;
    public const double FontMeta = 12;
    public const double FontSmall = 11;
    public const double ControlHeight = 28;
    public const double Radius = 4;
    public const double Space = 8;
    public static readonly Color Base = Color.Parse("#151515");
    public static readonly Color Rail = Color.Parse("#101010");
    public static readonly Color Card = Color.Parse("#222222");
    public static readonly Color CardHover = Color.Parse("#2C2C2C");
    public static readonly Color Border = Color.Parse("#3A3A3A");
    public static readonly Color Accent = Color.Parse("#0070E0");
    public static readonly Color TextTitle = Color.Parse("#E6E6E6");
    public static readonly Color TextMeta = Color.Parse("#B0B0B0");
    public static readonly Color TextMuted = Color.Parse("#8C8C8C");
    public static readonly Color TextDisabled = Color.Parse("#5A5A5A");
    public static readonly Color TextCode = Color.Parse("#00A0DA");
    public static readonly Color Bad = Color.Parse("#FF5555");
    public static readonly Color Warn = Color.Parse("#E0A030");
    public static readonly Color Good = Color.Parse("#3FB950");

    public static readonly Color OffSurface = Color.Parse("#2B2B2B");
    public static readonly Color OffEdge = Color.Parse("#3A3A3A");
    public static readonly Color OffEdgeStrong = Color.Parse("#6A6A6A");
    public static readonly Color OffText = Color.Parse("#9AA0A6");

    public static readonly Color RouteColour = Color.Parse("#58D0C0");

    public static readonly Color Wildcard = Color.Parse("#F778BA");

    public static readonly Color Casing = Base;

    public static readonly IBrush BaseBrush = new SolidColorBrush(Base);
    public static readonly IBrush RailBrush = new SolidColorBrush(Rail);
    public static readonly IBrush CardBrush = new SolidColorBrush(Card);
    public static readonly IBrush CardHoverBrush = new SolidColorBrush(CardHover);
    public static readonly IBrush BorderBrush = new SolidColorBrush(Border);
    public static readonly IBrush TitleBrush = new SolidColorBrush(TextTitle);
    public static readonly IBrush MetaBrush = new SolidColorBrush(TextMeta);
    public static readonly IBrush MutedBrush = new SolidColorBrush(TextMuted);
    public static readonly IBrush DisabledBrush = new SolidColorBrush(TextDisabled);
    public static readonly IBrush CodeBrush = new SolidColorBrush(TextCode);
    public static readonly IBrush AccentBrush = new SolidColorBrush(Accent);
    public static readonly IBrush BadBrush = new SolidColorBrush(Bad);
    public static readonly IBrush WarnBrush = new SolidColorBrush(Warn);
    public static readonly IBrush OffSurfaceBrush = new SolidColorBrush(OffSurface);
    public static readonly IBrush OffEdgeBrush = new SolidColorBrush(OffEdge);
    public static readonly IBrush OffEdgeStrongBrush = new SolidColorBrush(OffEdgeStrong);
    public static readonly IBrush OffTextBrush = new SolidColorBrush(OffText);

    public static TextBox Field(string watermark = "", double minWidth = 0, string name = "")
    {
        var box = new TextBox
        {
            Watermark = watermark,
            MinWidth = minWidth,
            MinHeight = ControlHeight,
            Background = CardBrush,
            Foreground = TitleBrush,
            BorderBrush = BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Radius),
            Padding = new Thickness(8, 5),
            FontSize = FontBody,
            VerticalContentAlignment = VerticalAlignment.Center,
        };
        return Named(box, name.Length > 0 ? name : watermark);
    }

    public static T Named<T>(T control, string name) where T : Control
    {
        if (name.Length > 0) AutomationProperties.SetName(control, name);
        return control;
    }

    public static Control Labelled(Control field, string label)
    {
        var caption = Label(label);
        caption.Margin = new Thickness(0, 0, 6, 0);
        AutomationProperties.SetLabeledBy(field, caption);
        if (AutomationProperties.GetName(field) is not { Length: > 0 }) Named(field, label);
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(caption);
        row.Children.Add(field);
        return row;
    }

    public static string ToolPanelName(string tool) => tool + " tool panel";

    public static Button Primary(string text) => Style(new Button { Content = text }, "primary");

    public static Button Secondary(string text) => Style(new Button { Content = text }, "secondary");

    private static Button Style(Button button, string cls)
    {
        button.Classes.Add(cls);
        button.BorderThickness = new Thickness(1);
        button.CornerRadius = new CornerRadius(Radius);
        button.Padding = new Thickness(12, 5);
        button.MinHeight = ControlHeight;
        button.FontSize = FontMeta;
        button.MinWidth = 84;
        button.HorizontalContentAlignment = HorizontalAlignment.Center;
        button.VerticalAlignment = VerticalAlignment.Center;
        return button;
    }

    public static ComboBox Combo(string name = "", double minWidth = 150)
    {
        var combo = new ComboBox
        {
            MinWidth = minWidth,
            MinHeight = ControlHeight,
            Background = CardBrush,
            BorderBrush = BorderBrush,
            Foreground = TitleBrush,
            FontSize = FontMeta,
            VerticalContentAlignment = VerticalAlignment.Center,
        };
        return Named(combo, name);
    }

    public static void Install(Application application)
    {
        application.Styles.Add(new Avalonia.Themes.Fluent.FluentTheme());
        foreach (var style in AppStyles()) application.Styles.Add(style);
    }

    public static IEnumerable<IStyle> AppStyles() => new IStyle[]
    {
        ButtonSkin("primary", AccentBrush, Brushes.White),
        ButtonSkin("secondary", CardBrush, MetaBrush),
        OffButton(),
        OffButtonSkin(),
        OffToggle(),
        OffToggleBox(),
        DropDownItem(),
    };

    private static Style ButtonSkin(string cls, IBrush background, IBrush foreground)
    {
        var style = new Style(selector => selector.OfType<Button>().Class(cls));
        style.Setters.Add(new Setter(TemplatedControl.BackgroundProperty, background));
        style.Setters.Add(new Setter(TemplatedControl.ForegroundProperty, foreground));
        style.Setters.Add(new Setter(TemplatedControl.BorderBrushProperty, BorderBrush));
        return style;
    }

    private static Style OffButton()
    {
        var style = new Style(selector => selector.OfType<Button>().Class(":disabled"));
        style.Setters.Add(new Setter(TemplatedControl.BackgroundProperty, OffSurfaceBrush));
        style.Setters.Add(new Setter(TemplatedControl.ForegroundProperty, OffTextBrush));
        style.Setters.Add(new Setter(TemplatedControl.BorderBrushProperty, OffEdgeBrush));
        return style;
    }

    private static Style OffButtonSkin()
    {
        var style = new Style(selector => selector.OfType<Button>().Class(":disabled")
            .Descendant().OfType<ContentPresenter>());
        style.Setters.Add(new Setter(ContentPresenter.BackgroundProperty, OffSurfaceBrush));
        style.Setters.Add(new Setter(ContentPresenter.BorderBrushProperty, OffEdgeBrush));
        style.Setters.Add(new Setter(TextElement.ForegroundProperty, OffTextBrush));
        return style;
    }

    private static Style OffToggle()
    {
        var style = new Style(selector => selector.OfType<CheckBox>().Class(":disabled")
            .Descendant().OfType<ContentPresenter>());
        style.Setters.Add(new Setter(TextElement.ForegroundProperty, OffTextBrush));
        return style;
    }

    private static Style OffToggleBox()
    {
        var style = new Style(selector => selector.OfType<CheckBox>().Class(":disabled")
            .Descendant().OfType<Border>());
        style.Setters.Add(new Setter(Avalonia.Controls.Border.BorderBrushProperty, OffEdgeStrongBrush));
        return style;
    }

    private static Style DropDownItem()
    {
        var style = new Style(selector => selector.OfType<ComboBoxItem>());
        style.Setters.Add(new Setter(TemplatedControl.ForegroundProperty, TitleBrush));
        return style;
    }

    public static TextBlock RowTitle(string text)
    {
        var title = SectionTitle(text);
        title.VerticalAlignment = VerticalAlignment.Center;
        return title;
    }

    public static Border Panel(string name, params Control[] rows)
    {
        var body = new StackPanel { Spacing = 6 };
        foreach (var row in rows) body.Children.Add(row);
        var panel = new Border
        {
            Background = CardBrush,
            BorderBrush = BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Radius),
            Padding = new Thickness(10, 6),
            VerticalAlignment = VerticalAlignment.Top,
            Child = body,
        };
        return Named(panel, name);
    }

    public static TextBlock SectionTitle(string text) => new()
    {
        Text = text.ToUpperInvariant(),
        Foreground = MutedBrush,
        FontSize = FontSmall,
        FontWeight = FontWeight.Bold,
        Margin = new Thickness(1, 2, 0, 2),
    };

    public static Border Group(string title, params Control[] rows)
    {
        var body = new StackPanel { Spacing = Space };
        body.Children.Add(SectionTitle(title));
        foreach (var row in rows) body.Children.Add(row);
        var group = new Border
        {
            Background = CardBrush,
            BorderBrush = BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Radius),
            Padding = new Thickness(10),
            VerticalAlignment = VerticalAlignment.Top,
            Child = body,
        };
        return Named(group, title);
    }

    public static WrapPanel Wrap(params Control[] controls)
    {
        var panel = new WrapPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        foreach (var control in controls)
        {
            control.Margin = new Thickness(0, 0, Space, Space);
            panel.Children.Add(control);
        }
        return panel;
    }

    public static TextBlock Label(string text) => new()
    {
        Text = text,
        Foreground = MetaBrush,
        FontSize = FontBody,
        VerticalAlignment = VerticalAlignment.Center,
    };

    public static Border Pill(TextBlock content)
    {
        content.Margin = new Thickness(9, 4);
        content.FontSize = FontMeta;
        return new Border
        {
            Background = CardBrush,
            BorderBrush = BorderBrush,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Radius),
            Child = content,
        };
    }

    public static readonly Color Machine = Accent;
    public static readonly Color StateInfo = Color.Parse("#79C0FF");
    public static readonly Color Transitions = Color.Parse("#A371F7");

    public static Color ForClass(string cls) => cls switch
    {
        "hkbStateMachine" => Machine,
        "hkbStateMachineStateInfo" => StateInfo,
        "hkbStateMachineTransitionInfoArray" => Transitions,
        _ => ByFamily(cls),
    };

    private static Color ByFamily(string cls)
    {
        if (cls.Contains("StateMachine")) return Machine;
        if (cls.Contains("ClipGenerator")) return Color.Parse("#3FB950");
        if (cls.Contains("Sequence")) return Color.Parse("#2EA043");
        if (cls.Contains("Blender") || cls.Contains("Layer") || cls.Contains("Selector"))
            return Color.Parse("#D29922");
        if (cls.Contains("Transition")) return Transitions;
        if (cls.Contains("Modifier")) return Color.Parse("#DB6D28");
        return TextMeta;
    }
}
