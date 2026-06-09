using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class UndoStackTests
{
    [Fact]
    public void PushApplyUndoRedo_RoundTrips()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        var dmg = rec.GetChild("Damage")!;
        var stack = new UndoStack();

        stack.Do(new FieldEditChange(dmg, "50", "120"));
        dmg.Value.Should().Be("120");

        stack.Undo();
        dmg.Value.Should().Be("50");
        stack.CanRedo.Should().BeTrue();

        stack.Redo();
        dmg.Value.Should().Be("120");
    }

    [Fact]
    public void NewChange_ClearsRedoStack()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001:T.esp", "Gun", ("Damage", "50"));
        var dmg = rec.GetChild("Damage")!;
        var stack = new UndoStack();
        stack.Do(new FieldEditChange(dmg, "50", "120"));
        stack.Undo();
        stack.Do(new FieldEditChange(dmg, "50", "75"));
        stack.CanRedo.Should().BeFalse();
    }
}
