using System;
using System.Collections.Generic;
using System.Linq;
using Flash;
using Flash.Display;
using Flash.Events;
using Flash.Filters;
using Flash.Geom;
using Flash.Text;
using Scaleform.Gfx;
using Xunit;

namespace Radicle.Interop.Tests;

[Collection("FlashStatics")]
public class FlashDisplayTests
{
    private const int Digits = 10;

    private static void AssertRect(double x, double y, double width, double height, Rectangle actual)
    {
        Assert.Equal(x, actual.X, Digits);
        Assert.Equal(y, actual.Y, Digits);
        Assert.Equal(width, actual.Width, Digits);
        Assert.Equal(height, actual.Height, Digits);
    }

    private static Sprite Box(double x, double y, double width, double height)
    {
        Sprite sprite = new Sprite();
        sprite.X = x;
        sprite.Y = y;
        sprite.Width = width;
        sprite.Height = height;
        return sprite;
    }

    private static Action<Event> Mark(List<string> log, string label) => e => log.Add(label);

    [Fact]
    public void DisplayObjectDefaultsAndUniqueNames()
    {
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        Assert.Equal(1.0, a.Alpha);
        Assert.Equal(1.0, a.ScaleX);
        Assert.Equal(1.0, a.ScaleY);
        Assert.Equal(0.0, a.Rotation);
        Assert.True(a.Visible);
        Assert.Equal("normal", a.BlendMode);
        Assert.StartsWith("instance", a.Name);
        Assert.NotEqual(a.Name, b.Name);
        Assert.Null(a.Parent);
        Assert.Null(a.Stage);
        Assert.Null(a.Root);
        Assert.True(a.MouseEnabled);
        Assert.Equal(-1, a.TabIndex);
        Assert.Equal(0, a.Filters.Length);
    }

    [Fact]
    public void LocalAndGlobalCoordinatesComposeAlongTheParentChain()
    {
        Sprite parent = new Sprite();
        parent.X = 100;
        parent.Y = 50;
        parent.ScaleX = 2;
        parent.ScaleY = 2;
        Sprite child = new Sprite();
        child.X = 10;
        child.Y = 10;
        parent.AddChild(child);
        Point global = child.LocalToGlobal(new Point(0, 0));
        Assert.Equal(120.0, global.X, Digits);
        Assert.Equal(70.0, global.Y, Digits);
        Point back = child.GlobalToLocal(new Point(130, 84));
        Assert.Equal(5.0, back.X, Digits);
        Assert.Equal(7.0, back.Y, Digits);
    }

    [Fact]
    public void RotationIsInDegrees()
    {
        Sprite s = new Sprite();
        s.Rotation = 90;
        Point p = s.LocalToGlobal(new Point(1, 0));
        Assert.Equal(0.0, p.X, Digits);
        Assert.Equal(1.0, p.Y, Digits);
    }

    [Fact]
    public void GlobalToLocalOfAZeroScaleObjectIsNaN()
    {
        Sprite s = new Sprite();
        s.ScaleX = 0;
        Point p = s.GlobalToLocal(new Point(1, 1));
        Assert.True(double.IsNaN(p.X));
        Assert.True(double.IsNaN(p.Y));
    }

    [Fact]
    public void GetBoundsInGlobalSelfAndParentSpace()
    {
        Sprite parent = Box(100, 0, 0, 0);
        Sprite child = Box(10, 20, 100, 50);
        AssertRect(10, 20, 100, 50, child.GetBounds(null));
        parent.AddChild(child);
        AssertRect(110, 20, 100, 50, child.GetBounds(null));
        AssertRect(0, 0, 100, 50, child.GetBounds(child));
        AssertRect(10, 20, 100, 50, child.GetBounds(parent));
        AssertRect(10, 20, 100, 50, child.GetRect(parent));
    }

    [Fact]
    public void GetBoundsOfARotatedObjectCoversItsCorners()
    {
        Sprite s = Box(0, 0, 100, 50);
        s.Rotation = 90;
        AssertRect(-50, 0, 50, 100, s.GetBounds(null));
    }

    [Fact]
    public void ContainerBoundsUnionTheChildren()
    {
        Sprite box = Box(100, 0, 0, 0);
        Sprite first = Box(10, 10, 20, 20);
        Sprite second = Box(40, 50, 10, 10);
        box.AddChild(first);
        AssertRect(110, 10, 20, 20, box.GetBounds(null));
        box.AddChild(second);
        AssertRect(110, 10, 40, 50, box.GetBounds(null));
    }

    [Fact]
    public void HitTestPointUsesTheLocalBoxWithExclusiveFarEdges()
    {
        Sprite s = Box(10, 10, 20, 20);
        Assert.True(s.HitTestPoint(15, 15));
        Assert.True(s.HitTestPoint(10, 10));
        Assert.False(s.HitTestPoint(5, 5));
        Assert.False(s.HitTestPoint(30, 30));
        Sprite holder = new Sprite();
        Sprite visible = Box(0, 0, 10, 10);
        Sprite hidden = Box(50, 50, 10, 10);
        hidden.Visible = false;
        holder.AddChild(visible);
        holder.AddChild(hidden);
        Assert.True(holder.HitTestPoint(5, 5));
        Assert.False(holder.HitTestPoint(55, 55));
    }

    [Fact]
    public void HitTestObjectComparesGlobalBounds()
    {
        Sprite a = Box(0, 0, 10, 10);
        Assert.True(a.HitTestObject(Box(5, 5, 10, 10)));
        Assert.False(a.HitTestObject(Box(10, 0, 10, 10)));
        Assert.False(a.HitTestObject(Box(50, 50, 10, 10)));
    }

    [Fact]
    public void StageRootAndMousePosition()
    {
        Stage stage = new Stage();
        Sprite parent = new Sprite();
        parent.X = 100;
        parent.Y = 50;
        parent.ScaleX = 2;
        parent.ScaleY = 2;
        Sprite child = new Sprite();
        child.X = 10;
        child.Y = 10;
        parent.AddChild(child);
        Assert.Null(child.Stage);
        Assert.Equal(0.0, child.MouseX);
        stage.AddChild(parent);
        stage.SetMouse(130, 84);
        Assert.Same(stage, child.Stage);
        Assert.Same(stage, stage.Stage);
        Assert.Same(parent, child.Root);
        Assert.Null(stage.Root);
        Assert.Equal(5.0, child.MouseX, Digits);
        Assert.Equal(7.0, child.MouseY, Digits);
    }

    [Fact]
    public void ChildrenKeepOrderIndexAndNames()
    {
        Sprite p = new Sprite();
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        Sprite c = new Sprite();
        b.Name = "middle";
        p.AddChild(a);
        p.AddChild(c);
        p.AddChildAt(b, 1);
        Assert.Equal(3, p.NumChildren);
        Assert.Same(a, p.GetChildAt(0));
        Assert.Same(b, p.GetChildAt(1));
        Assert.Equal(2, p.GetChildIndex(c));
        Assert.Same(b, p.GetChildByName("middle"));
        Assert.Null(p.GetChildByName("absent"));
        Assert.Same(p, a.Parent);
        Assert.Throws<ArgumentOutOfRangeException>(() => p.GetChildAt(3));
        Assert.Throws<ArgumentOutOfRangeException>(() => p.AddChildAt(new Sprite(), 9));
    }

    [Fact]
    public void ReorderingChildren()
    {
        Sprite p = new Sprite();
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        Sprite c = new Sprite();
        p.AddChild(a);
        p.AddChild(b);
        p.AddChild(c);
        p.SetChildIndex(a, 2);
        Assert.Equal(new DisplayObject[] { b, c, a }, Enumerable.Range(0, 3).Select(p.GetChildAt));
        p.SwapChildren(b, a);
        Assert.Equal(new DisplayObject[] { a, c, b }, Enumerable.Range(0, 3).Select(p.GetChildAt));
        p.SwapChildrenAt(0, 1);
        Assert.Same(c, p.GetChildAt(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => p.SetChildIndex(a, 3));
    }

    [Fact]
    public void AddingToTheSameParentMovesTheChildWithoutANewAddedEvent()
    {
        Sprite p = new Sprite();
        Sprite first = new Sprite();
        Sprite second = new Sprite();
        List<string> log = new List<string>();
        first.AddEventListener(Event.ADDED, Mark(log, "added"));
        p.AddChild(first);
        p.AddChild(second);
        p.AddChild(first);
        Assert.Equal(2, p.NumChildren);
        Assert.Equal(1, p.GetChildIndex(first));
        Assert.Single(log);
    }

    [Fact]
    public void ReparentingRemovesTheChildFromItsOldParent()
    {
        Sprite one = new Sprite();
        Sprite two = new Sprite();
        Sprite c = new Sprite();
        List<string> log = new List<string>();
        one.AddChild(c);
        c.AddEventListener(Event.REMOVED, Mark(log, "removed"));
        two.AddChild(c);
        Assert.Equal(0, one.NumChildren);
        Assert.Equal(1, two.NumChildren);
        Assert.Same(two, c.Parent);
        Assert.Equal(new[] { "removed" }, log);
    }

    [Fact]
    public void CyclesAndForeignChildrenAreRejected()
    {
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        a.AddChild(b);
        Assert.Throws<ArgumentException>(() => b.AddChild(a));
        Assert.Throws<ArgumentException>(() => a.AddChild(a));
        Assert.Throws<ArgumentException>(() => b.RemoveChild(a));
        Assert.Throws<ArgumentException>(() => a.GetChildIndex(new Sprite()));
    }

    [Fact]
    public void AddedEventBubblesAndAddedToStageReachesTheWholeSubtreeOnce()
    {
        Stage stage = new Stage();
        Sprite box = new Sprite();
        stage.AddChild(box);
        Sprite child = new Sprite();
        List<string> log = new List<string>();
        Action<Event> boxAdded = e => log.Add("box:" + e.EventPhase + ":" + ReferenceEquals(e.Target, child));
        box.AddEventListener(Event.ADDED, boxAdded);
        child.AddEventListener(Event.ADDED, Mark(log, "child"));
        child.AddEventListener(Event.ADDED_TO_STAGE, Mark(log, "child-on-stage"));
        box.AddChild(child);
        Assert.Equal(new[] { "child", "box:3:True", "child-on-stage" }, log);

        Sprite branch = new Sprite();
        Sprite leaf = new Sprite();
        log.Clear();
        branch.AddEventListener(Event.ADDED_TO_STAGE, Mark(log, "branch"));
        leaf.AddEventListener(Event.ADDED_TO_STAGE, Mark(log, "leaf"));
        branch.AddChild(leaf);
        Assert.Empty(log);
        stage.AddChild(branch);
        Assert.Equal(new[] { "branch", "leaf" }, log);
    }

    [Fact]
    public void RemovalFiresRemovedBeforeRemovedFromStageWhileStillOnStage()
    {
        Stage stage = new Stage();
        Sprite branch = new Sprite();
        Sprite leaf = new Sprite();
        branch.AddChild(leaf);
        stage.AddChild(branch);
        List<string> log = new List<string>();
        branch.AddEventListener(Event.REMOVED, Mark(log, "removed"));
        Action<Event> branchOff = e => log.Add("branch-off:" + (branch.Stage != null));
        Action<Event> leafOff = e => log.Add("leaf-off:" + (leaf.Stage != null));
        branch.AddEventListener(Event.REMOVED_FROM_STAGE, branchOff);
        leaf.AddEventListener(Event.REMOVED_FROM_STAGE, leafOff);
        Assert.Same(branch, stage.RemoveChild(branch));
        Assert.Equal(new[] { "removed", "branch-off:True", "leaf-off:True" }, log);
        Assert.Null(branch.Parent);
        Assert.Null(leaf.Stage);
    }

    [Fact]
    public void RemoveChildrenTakesAnInclusiveRange()
    {
        Sprite p = new Sprite();
        Sprite[] kids = { new Sprite(), new Sprite(), new Sprite(), new Sprite() };
        foreach (Sprite kid in kids)
        {
            p.AddChild(kid);
        }
        p.RemoveChildren(1, 2);
        Assert.Equal(2, p.NumChildren);
        Assert.Same(kids[0], p.GetChildAt(0));
        Assert.Same(kids[3], p.GetChildAt(1));
        p.RemoveChildren();
        Assert.Equal(0, p.NumChildren);
        Assert.Null(kids[0].Parent);
        p.RemoveChildren();
    }

    [Fact]
    public void ContainsIncludesTheContainerItselfAndDescendants()
    {
        Sprite a = new Sprite();
        Sprite b = new Sprite();
        Sprite c = new Sprite();
        a.AddChild(b);
        b.AddChild(c);
        Assert.True(a.Contains(a));
        Assert.True(a.Contains(c));
        Assert.False(b.Contains(a));
    }

    [Fact]
    public void MovieClipStartsOnFrameOneAndPlaying()
    {
        MovieClip mc = new MovieClip();
        Assert.Equal(1, mc.CurrentFrame);
        Assert.Equal(1, mc.TotalFrames);
        Assert.Equal(1, mc.FramesLoaded);
        Assert.True(mc.IsPlaying);
        Assert.Null(mc.CurrentLabel);
    }

    [Fact]
    public void GotoClampsNumbersAndSetsThePlayState()
    {
        MovieClip mc = new MovieClip();
        mc.TotalFrames = 10;
        mc.GotoAndStop(5);
        Assert.Equal(5, mc.CurrentFrame);
        Assert.False(mc.IsPlaying);
        mc.GotoAndPlay(3);
        Assert.Equal(3, mc.CurrentFrame);
        Assert.True(mc.IsPlaying);
        mc.GotoAndStop(99);
        Assert.Equal(10, mc.CurrentFrame);
        mc.GotoAndStop(0);
        Assert.Equal(1, mc.CurrentFrame);
        mc.GotoAndStop(2.9);
        Assert.Equal(2, mc.CurrentFrame);
        Assert.Throws<ArgumentException>(() => mc.GotoAndStop(double.NaN));
    }

    [Fact]
    public void GotoByLabelAndCurrentLabel()
    {
        MovieClip mc = new MovieClip();
        mc.TotalFrames = 10;
        mc.FrameLabels = new AsArray { new FrameLabel("intro", 1), new FrameLabel("loop", 5) };
        Assert.Equal("intro", mc.CurrentLabel);
        mc.GotoAndStop("loop");
        Assert.Equal(5, mc.CurrentFrame);
        mc.GotoAndStop(7);
        Assert.Equal("loop", mc.CurrentLabel);
        mc.GotoAndPlay(3);
        Assert.Equal("intro", mc.CurrentLabel);
        Assert.Throws<ArgumentException>(() => mc.GotoAndStop("missing"));
        Assert.Throws<ArgumentException>(() => mc.GotoAndStop("7"));
    }

    [Fact]
    public void NextAndPrevFrameStopPlaybackAndClamp()
    {
        MovieClip mc = new MovieClip();
        mc.TotalFrames = 3;
        mc.PrevFrame();
        Assert.Equal(1, mc.CurrentFrame);
        Assert.False(mc.IsPlaying);
        mc.NextFrame();
        mc.NextFrame();
        mc.NextFrame();
        Assert.Equal(3, mc.CurrentFrame);
        mc.Play();
        Assert.True(mc.IsPlaying);
        mc.Stop();
        Assert.False(mc.IsPlaying);
    }

    [Fact]
    public void AdvanceFrameLoopsWhilePlayingAndAlwaysDispatchesEnterFrame()
    {
        MovieClip mc = new MovieClip();
        mc.TotalFrames = 3;
        int frames = 0;
        Action<Event> counter = e => frames++;
        mc.AddEventListener(Event.ENTER_FRAME, counter);
        mc.AdvanceFrame();
        Assert.Equal(2, mc.CurrentFrame);
        mc.AdvanceFrame();
        mc.AdvanceFrame();
        Assert.Equal(1, mc.CurrentFrame);
        mc.Stop();
        mc.AdvanceFrame();
        Assert.Equal(1, mc.CurrentFrame);
        Assert.Equal(4, frames);
    }

    [Fact]
    public void ShrinkingTotalFramesClampsTheCurrentFrame()
    {
        MovieClip mc = new MovieClip();
        mc.TotalFrames = 10;
        mc.GotoAndStop(8);
        mc.TotalFrames = 3;
        Assert.Equal(3, mc.CurrentFrame);
        mc.TotalFrames = 0;
        Assert.Equal(1, mc.TotalFrames);
        Assert.Equal(1, mc.CurrentFrame);
        Flash.Display.Scene scene = new Flash.Display.Scene("main", 3);
        Assert.Equal("main", scene.Name);
        Assert.Equal(3, scene.NumFrames);
        Assert.Equal(0, scene.Labels.Length);
    }

    [Fact]
    public void SpriteRecordsDragStateAndDrawingCommands()
    {
        Sprite s = new Sprite();
        Assert.True(s.UseHandCursor);
        Assert.False(s.ButtonMode);
        Rectangle bounds = new Rectangle(0, 0, 10, 10);
        s.StartDrag(true, bounds);
        Assert.True(s.IsDragging);
        Assert.True(s.DragLockCenter);
        Assert.Same(bounds, s.DragBounds);
        s.StopDrag();
        Assert.False(s.IsDragging);
        Assert.Null(s.DragBounds);

        s.Graphics.BeginFill(0xFF0000);
        s.Graphics.DrawRect(0, 0, 10, 20);
        s.Graphics.EndFill();
        Assert.Equal(new[] { "beginFill", "drawRect", "endFill" }, s.Graphics.Commands.Select(c => c.Name));
        Assert.Equal(new double[] { 0, 0, 10, 20 }, s.Graphics.Commands[1].Args);
        s.Graphics.Clear();
        Assert.Empty(s.Graphics.Commands);
    }

    [Fact]
    public void StageDefaultsAndConstants()
    {
        Stage stage = new Stage();
        Assert.Equal(12.0, stage.FrameRate);
        Assert.Equal(StageScaleMode.SHOW_ALL, stage.ScaleMode);
        Assert.Equal(StageAlign.NONE, stage.Align);
        Assert.Equal(StageQuality.HIGH, stage.Quality);
        Assert.Equal(StageDisplayState.NORMAL, stage.DisplayState);
        Assert.Equal("TL", StageAlign.TOP_LEFT);
        Assert.Equal("noScale", StageScaleMode.NO_SCALE);
        Assert.Equal("multiply", BlendMode.MULTIPLY);
        Assert.Equal("fullScreen", StageDisplayState.FULL_SCREEN);
    }

    [Fact]
    public void BitmapButtonAndLoaderHoldTheirProperties()
    {
        BitmapData data = new BitmapData(10, 20, false, 0xFF00FF00);
        Assert.Equal(10, data.Width);
        Assert.Equal(20, data.Height);
        Assert.False(data.Transparent);
        Assert.Equal(0xFF00FF00u, data.FillColor);
        Bitmap bitmap = new Bitmap();
        bitmap.BitmapData = data;
        Assert.Same(data, bitmap.BitmapData);
        Assert.Equal("auto", bitmap.PixelSnapping);
        SimpleButton button = new SimpleButton();
        Assert.True(button.UseHandCursor);
        Assert.Null(button.UpState);
        Loader loader = new Loader();
        Assert.NotNull(loader.ContentLoaderInfo);
        Assert.Equal(string.Empty, loader.ContentLoaderInfo.Url);
    }

    [Fact]
    public void TextFieldTextAndHtmlStayInSync()
    {
        TextField tf = new TextField();
        tf.Text = "a<b";
        Assert.Equal("a&lt;b", tf.HtmlText);
        tf.HtmlText = "<b>Hi</b><br/>there &amp; co";
        Assert.Equal("Hi\rthere & co", tf.Text);
        Assert.Equal("<b>Hi</b><br/>there &amp; co", tf.HtmlText);
        Assert.Equal(2, tf.NumLines);
        tf.Text = "plain";
        Assert.Equal("plain", tf.HtmlText);
        Assert.Equal(5, tf.Length);
    }

    [Fact]
    public void TextFieldDefaultsAndColor()
    {
        TextField tf = new TextField();
        Assert.Equal(string.Empty, tf.Text);
        Assert.True(tf.Selectable);
        Assert.Equal(TextFieldAutoSize.NONE, tf.AutoSize);
        Assert.Equal(TextFieldType.DYNAMIC, tf.Type);
        Assert.Equal(AntiAliasType.NORMAL, tf.AntiAliasType);
        Assert.Equal(1, tf.ScrollV);
        Assert.Equal(1, tf.NumLines);
        tf.TextColor = 0x00FF80;
        Assert.Equal(0x00FF80u, tf.TextColor);
        tf.Text = null!;
        Assert.Equal(string.Empty, tf.Text);
    }

    [Fact]
    public void TextFieldLineCountingTreatsCrLfAsOneBreak()
    {
        TextField tf = new TextField();
        tf.Text = "a\r\nb\nc\rd";
        Assert.Equal(4, tf.NumLines);
    }

    [Fact]
    public void TextFieldAppendAndReplace()
    {
        TextField tf = new TextField();
        tf.Text = "abcdef";
        tf.ReplaceText(1, 3, "X");
        Assert.Equal("aXdef", tf.Text);
        tf.ReplaceText(-5, 100, "Z");
        Assert.Equal("Z", tf.Text);
        tf.AppendText("<q>");
        Assert.Equal("Z<q>", tf.Text);
        tf.HtmlText = "<b>x</b>";
        tf.AppendText("&");
        Assert.Equal("x&", tf.Text);
        Assert.Equal("<b>x</b>&amp;", tf.HtmlText);
    }

    [Fact]
    public void TextFormatMergesNonNullFieldsAndCloneIsIndependent()
    {
        TextField tf = new TextField();
        tf.SetTextFormat(new TextFormat(font: "Arial", size: 12));
        tf.SetTextFormat(new TextFormat(color: 0xFF0000));
        TextFormat read = tf.GetTextFormat();
        Assert.Equal("Arial", read.Font);
        Assert.Equal(12, read.Size);
        Assert.Equal(0xFF0000, read.Color);
        read.Font = "Courier";
        Assert.Equal("Arial", tf.GetTextFormat().Font);
        Assert.Null(tf.GetTextFormat().Bold);
    }

    [Fact]
    public void StyleSheetStoresNamedStyles()
    {
        StyleSheet sheet = new StyleSheet();
        AsObject style = new AsObject { ["color"] = "#f00" };
        sheet.SetStyle("a", style);
        sheet.SetStyle("b", new AsObject());
        Assert.Equal("a,b", sheet.StyleNames.Join());
        Assert.Same(style, sheet.GetStyle("a"));
        Assert.Null(sheet.GetStyle("missing"));
        sheet.Clear();
        Assert.Equal(0, sheet.StyleNames.Length);
    }

    [Fact]
    public void FiltersCloneIndependently()
    {
        BlurFilter blur = new BlurFilter(2, 3, 2);
        BlurFilter blurCopy = Assert.IsType<BlurFilter>(blur.Clone());
        blurCopy.BlurX = 50;
        Assert.Equal(2.0, blur.BlurX);
        Assert.Equal(3.0, blurCopy.BlurY);
        Assert.Equal(2, blurCopy.Quality);
        DropShadowFilter shadow = Assert.IsType<DropShadowFilter>(new DropShadowFilter().Clone());
        Assert.Equal(4.0, shadow.Distance);
        Assert.Equal(45.0, shadow.Angle);
        GlowFilter glow = Assert.IsType<GlowFilter>(new GlowFilter().Clone());
        Assert.Equal(0xFF0000u, glow.Color);
        Assert.Equal(6.0, glow.BlurX);
        ColorMatrixFilter matrix = Assert.IsType<ColorMatrixFilter>(new ColorMatrixFilter(new AsArray { 1, 0, 0 }).Clone());
        Assert.Equal("1,0,0", matrix.Matrix!.Join());
    }

    [Fact]
    public void ScaleformTextFieldHelpersWriteThroughToTheField()
    {
        TextField tf = new TextField();
        TextFieldEx.SetVerticalAlign(tf, "center");
        TextFieldEx.SetVerticalAutoSize(tf, "bottom");
        Assert.Equal("center", tf.VerticalAlign);
        Assert.Equal("bottom", tf.VerticalAutoSize);
        tf.HtmlText = "<b>a</b>";
        TextFieldEx.AppendHtml(tf, "<i>b</i>");
        Assert.Equal("<b>a</b><i>b</i>", tf.HtmlText);
        Assert.Equal("ab", tf.Text);
        object marker = new object();
        TextFieldEx.SetImageSubstitutions(tf, marker);
        Assert.Same(marker, tf.ImageSubstitutions);
    }

    [Fact]
    public void ScaleformExtensionsFlagsAreSettableStatics()
    {
        bool enabled = Scaleform.Gfx.Extensions.Enabled;
        try
        {
            Scaleform.Gfx.Extensions.Enabled = true;
            Assert.True(Scaleform.Gfx.Extensions.Enabled);
        }
        finally
        {
            Scaleform.Gfx.Extensions.Enabled = enabled;
        }
    }

    [Fact]
    public void ScaleformEventSubclassesCarryTheControllerIndexThroughClone()
    {
        MouseEventEx mouse = new MouseEventEx(MouseEvent.CLICK, true, false, 3);
        Assert.Equal(3u, Assert.IsType<MouseEventEx>(mouse.Clone()).ControllerIdx);
        FocusEventEx focus = new FocusEventEx(FocusEvent.FOCUS_IN, true, false, 2);
        Assert.Equal(2u, Assert.IsType<FocusEventEx>(focus.Clone()).ControllerIdx);
        KeyboardEventEx key = new KeyboardEventEx(KeyboardEvent.KEY_UP, true, false, 1);
        Assert.Equal(1u, Assert.IsType<KeyboardEventEx>(key.Clone()).ControllerIdx);
        Assert.IsAssignableFrom<MouseEvent>(mouse);
    }
}
