using System;
using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services.Papyrus;

namespace FO4RecordEditor.Core.Tests;

public sealed class PapyrusCreationKitFidelityTests : IDisposable
{
    private readonly string _root;

    public PapyrusCreationKitFidelityTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "fo4re-ck-fidelity-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_root);
        Write("ScriptObject", """
            Scriptname ScriptObject Native Hidden
            Function GotoState(string asNewState) native
            Function RegisterForCustomEvent(ScriptObject akSender, CustomEventName asEventName) native
            Function SendCustomEvent(CustomEventName asEvent, Var[] akArgs = None) native
            """);
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch { }
    }

    [Fact]
    public void Custom_event_literals_use_the_lowercase_declaring_script_prefix()
    {
        Write("EventSource", """
            Scriptname EventSource extends ScriptObject
            CustomEvent Finished
            """);
        Write("CustomEventConsumer", """
            Scriptname CustomEventConsumer extends ScriptObject
            Function Go(EventSource akSource)
                RegisterForCustomEvent(akSource, "Finished")
                akSource.SendCustomEvent("Finished")
            EndFunction
            """);

        var function = CompileFunction("CustomEventConsumer", "Go");
        var eventNames = function.Instructions
            .SelectMany(instruction => instruction.Args)
            .Where(value => value.Type == PexValueType.String)
            .Select(value => value.Str)
            .ToArray();

        eventNames.Should().Equal("eventsource_Finished", "eventsource_Finished");
    }

    [Fact]
    public void Bool_literal_comparison_uses_true_without_casting_one()
    {
        Write("BoolCondition", """
            Scriptname BoolCondition extends ScriptObject
            bool Function GetStageDone(int aiStage)
                return false
            EndFunction
            Function Go()
                bool abUnconscious = GetStageDone(100) == 1
            EndFunction
            """);

        var instructions = CompileFunction("BoolCondition", "Go").Instructions;
        var comparison = instructions.Single(instruction => instruction.Mnemonic == "cmp_eq");

        comparison.Args[2].Type.Should().Be(PexValueType.Bool);
        comparison.Args[2].Bool.Should().BeTrue();
        instructions.Should().NotContain(instruction =>
            instruction.Mnemonic == "cast"
            && instruction.Args.Any(value => value.Type == PexValueType.Integer && value.Int == 1));
    }

    [Fact]
    public void Nested_read_uses_own_auto_property_backing_variable_as_receiver()
    {
        Write("WorkshopParentScript", """
            Scriptname WorkshopParentScript extends ScriptObject
            bool Property PlayerInstitute_Destroyed Auto
            """);
        Write("PropertyRoute", """
            Scriptname PropertyRoute extends ScriptObject
            WorkshopParentScript Property WorkshopParent Auto
            Function Go()
                bool value = WorkshopParent.PlayerInstitute_Destroyed
            EndFunction
            """);

        var reads = CompileFunction("PropertyRoute", "Go").Instructions
            .Where(instruction => instruction.Mnemonic == "propget")
            .ToArray();

        reads.Should().ContainSingle();
        reads[0].Args[0].Str.Should().BeEquivalentTo("PlayerInstitute_Destroyed");
        reads[0].Args[1].Str.Should().Be("::WorkshopParent_var");
    }

    [Fact]
    public void Nested_read_uses_inherited_auto_property_backing_variable_as_receiver()
    {
        Write("WorkshopParentScript", """
            Scriptname WorkshopParentScript extends ScriptObject
            bool Property PlayerInstitute_Destroyed Auto
            """);
        Write("WorkshopAttackScript", """
            Scriptname WorkshopAttackScript extends ScriptObject
            WorkshopParentScript Property WorkshopParent Auto
            """);
        Write("WorkshopSynthAttackScript", """
            Scriptname WorkshopSynthAttackScript extends WorkshopAttackScript
            Function Go()
                bool value = WorkshopParent.PlayerInstitute_Destroyed
            EndFunction
            """);

        var reads = CompileFunction("WorkshopSynthAttackScript", "Go").Instructions
            .Where(instruction => instruction.Mnemonic == "propget")
            .ToArray();

        reads.Should().ContainSingle();
        reads[0].Args[0].Str.Should().BeEquivalentTo("PlayerInstitute_Destroyed");
        reads[0].Args[1].Str.Should().Be("::WorkshopParent_var");
    }

    [Fact]
    public void Write_to_inherited_auto_property_assigns_the_backing_variable()
    {
        Write("BaseHolderScript", """
            Scriptname BaseHolderScript extends ScriptObject
            int Property Counter Auto
            """);
        Write("DerivedWriterScript", """
            Scriptname DerivedWriterScript extends BaseHolderScript
            Function Go()
                Counter = 7
            EndFunction
            """);

        var instructions = CompileFunction("DerivedWriterScript", "Go").Instructions;

        instructions.Should().NotContain(i => i.Mnemonic == "propset",
            "an inherited Auto property is written through its backing variable, not a propset");
        var write = instructions.Single(i => i.Mnemonic == "assign" && i.Args[0].Str == "::Counter_var");
        write.Args[1].Int.Should().Be(7);
    }

    [Fact]
    public void GotoState_literal_is_canonicalized_to_the_declared_state_and_survives_pex_round_trip()
    {
        Write("StateCase", """
            Scriptname StateCase extends ScriptObject
            Function Go()
                GotoState("glassFLIPPED")
            EndFunction
            State GlassFlipped
            EndState
            """);

        var emitted = CompilePex("StateCase");
        StateArgument(Function(emitted, "Go")).Should().Be("GlassFlipped");

        var reloaded = PexFile.FromBytes(emitted.ToBytes());
        StateArgument(Function(reloaded, "Go")).Should().Be("GlassFlipped");
    }

    [Fact]
    public void GotoState_literal_matching_a_lowercase_declaration_is_lowered()
    {
        Write("StateCaseLower", """
            Scriptname StateCaseLower extends ScriptObject
            Function Go()
                GotoState("Off")
            EndFunction
            State off
            EndState
            """);

        StateArgument(CompileFunction("StateCaseLower", "Go")).Should().Be("off");
    }

    [Fact]
    public void GotoState_override_keeps_ordinary_string_argument_spelling()
    {
        Write("StateOverride", """
            Scriptname StateOverride extends ScriptObject
            Function GotoState(string asValue)
            EndFunction
            Function Go()
                GotoState("KeepCase")
            EndFunction
            """);

        StateArgument(CompileFunction("StateOverride", "Go")).Should().Be("KeepCase");
    }

    [Fact]
    public void GotoState_on_an_external_receiver_keeps_its_spelling()
    {
        Write("StateTarget", "Scriptname StateTarget extends ScriptObject\n");
        Write("ExternalStateCase", """
            Scriptname ExternalStateCase extends ScriptObject
            Function Go(StateTarget akTarget)
                akTarget.GotoState("MixedCase")
            EndFunction
            """);

        StateArgument(CompileFunction("ExternalStateCase", "Go")).Should().Be("MixedCase");
    }

    [Fact]
    public void Negative_numeric_literals_are_folded_into_assign_instructions()
    {
        Write("NegativeConstants", """
            Scriptname NegativeConstants extends ScriptObject
            Function Go()
                int i = -1
                float f = -2.5
            EndFunction
            """);

        var instructions = CompileFunction("NegativeConstants", "Go").Instructions;
        instructions.Should().HaveCount(2);

        instructions[0].Mnemonic.Should().Be("assign");
        instructions[0].Args[0].Str.Should().Be("i");
        instructions[0].Args[1].Type.Should().Be(PexValueType.Integer);
        instructions[0].Args[1].Int.Should().Be(-1);

        instructions[1].Mnemonic.Should().Be("assign");
        instructions[1].Args[0].Str.Should().Be("f");
        instructions[1].Args[1].Type.Should().Be(PexValueType.Float);
        instructions[1].Args[1].Float.Should().Be(-2.5f);
    }

    private static string StateArgument(PexFunction function) =>
        function.Instructions.Single(instruction => instruction.Mnemonic == "callmethod").Args[3].Str;

    private void Write(string scriptName, string source) =>
        File.WriteAllText(Path.Combine(_root, scriptName + ".psc"), source);

    private PexFunction CompileFunction(string scriptName, string functionName) =>
        Function(CompilePex(scriptName), functionName);

    private PexFile CompilePex(string scriptName)
    {
        var index = new PapyrusScriptIndex();
        index.AddRoot(_root);
        var result = new PapyrusCompiler(index).CompileFile(Path.Combine(_root, scriptName + ".psc"));

        result.Success.Should().BeTrue(
            "compilation should succeed; diagnostics were: "
            + string.Join(" | ", result.Diagnostics.Select(diagnostic => diagnostic.ToString())));

        return result.Pex!;
    }

    private static PexFunction Function(PexFile pex, string functionName) =>
        pex.Objects.Single().States.Single(state => state.Name == string.Empty).Functions
            .Single(function => function.Name.Equals(functionName, StringComparison.OrdinalIgnoreCase));
}
