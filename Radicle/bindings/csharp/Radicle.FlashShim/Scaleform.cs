using Flash.Events;
using Flash.Text;

namespace Scaleform.Gfx
{
    public static class Extensions
    {
        public static bool Enabled { get; set; }

        public static bool NoInvisibleAdvance { get; set; }
    }

    public static class TextFieldEx
    {
        public static void SetVerticalAlign(TextField textField, string valign) => textField.VerticalAlign = valign;

        public static void SetVerticalAutoSize(TextField textField, string vautoSize) =>
            textField.VerticalAutoSize = vautoSize;

        public static void AppendHtml(TextField textField, string html) => textField.HtmlText = textField.HtmlText + html;

        public static void SetImageSubstitutions(TextField textField, object? substitutions) =>
            textField.ImageSubstitutions = substitutions;
    }

    public class FocusEventEx : FocusEvent
    {
        public FocusEventEx(string type, bool bubbles = true, bool cancelable = false, uint controllerIdx = 0)
            : base(type, bubbles, cancelable)
        {
            ControllerIdx = controllerIdx;
        }

        public uint ControllerIdx { get; set; }

        public override Event Clone() => new FocusEventEx(Type, Bubbles, Cancelable, ControllerIdx);
    }

    public class MouseEventEx : MouseEvent
    {
        public MouseEventEx(string type, bool bubbles = true, bool cancelable = false, uint controllerIdx = 0)
            : base(type, bubbles, cancelable)
        {
            ControllerIdx = controllerIdx;
        }

        public uint ControllerIdx { get; set; }

        public override Event Clone() => new MouseEventEx(Type, Bubbles, Cancelable, ControllerIdx);
    }

    public class KeyboardEventEx : KeyboardEvent
    {
        public KeyboardEventEx(string type, bool bubbles = true, bool cancelable = false, uint controllerIdx = 0)
            : base(type, bubbles, cancelable)
        {
            ControllerIdx = controllerIdx;
        }

        public uint ControllerIdx { get; set; }

        public override Event Clone() => new KeyboardEventEx(Type, Bubbles, Cancelable, ControllerIdx);
    }

    public static class InteractiveObjectEx
    {
    }

    public static class DisplayObjectEx
    {
    }

    public static class SelectionEx
    {
    }

    public static class IMEEx
    {
    }
}
