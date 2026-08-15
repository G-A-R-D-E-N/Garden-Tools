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
    public void GotoState_literal_is_lowercased_and_survives_pex_round_trip()
    {
        Write("StateCase", """
            Scriptname StateCase extends ScriptObject
            Function Go()
                GotoState("GlassFlipped")
            EndFunction
            State GlassFlipped
            EndState
            """);

        var emitted = CompilePex("StateCase");
        StateArgument(Function(emitted, "Go")).Should().Be("glassflipped");

        var reloaded = PexFile.FromBytes(emitted.ToBytes());
        StateArgument(Function(reloaded, "Go")).Should().Be("glassflipped");
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

    [Fact]
    public void String_table_uses_case_insensitive_first_writer_spelling()
    {
        Write("StringCase", """
            Scriptname StringCase extends ScriptObject
            Function Go()
                string first = "SPECIALMenu"
                string second = "SpecialMenu"
            EndFunction
            """);

        var emitted = CompilePex("StringCase");
        emitted.StringTable
            .Where(value => value.Equals("SPECIALMenu", StringComparison.OrdinalIgnoreCase))
            .Should().Equal("SPECIALMenu");

        var reloaded = PexFile.FromBytes(emitted.ToBytes());
        var literalValues = Function(reloaded, "Go").Instructions
            .SelectMany(instruction => instruction.Args)
            .Where(value => value.Type == PexValueType.String)
            .Select(value => value.Str)
            .ToArray();

        literalValues.Should().Equal("SPECIALMenu", "SPECIALMenu");
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
