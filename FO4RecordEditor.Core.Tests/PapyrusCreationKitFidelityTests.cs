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

    private void Write(string scriptName, string source) =>
        File.WriteAllText(Path.Combine(_root, scriptName + ".psc"), source);

    private PexFunction CompileFunction(string scriptName, string functionName)
    {
        var index = new PapyrusScriptIndex();
        index.AddRoot(_root);
        var result = new PapyrusCompiler(index).CompileFile(Path.Combine(_root, scriptName + ".psc"));

        result.Success.Should().BeTrue(
            "compilation should succeed; diagnostics were: "
            + string.Join(" | ", result.Diagnostics.Select(diagnostic => diagnostic.ToString())));

        return result.Pex!.Objects.Single().States
            .Single(state => state.Name == string.Empty).Functions
            .Single(function => function.Name.Equals(functionName, StringComparison.OrdinalIgnoreCase));
    }
}
