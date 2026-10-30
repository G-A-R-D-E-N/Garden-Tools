using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Core.Tests;

public sealed class NifServiceNoBackendTests : IDisposable
{
    private readonly string _temp = Path.Combine(
        Path.GetTempPath(),
        "fo4re-nif-no-backend-" + Guid.NewGuid().ToString("N"));

    public NifServiceNoBackendTests() => Directory.CreateDirectory(_temp);

    public void Dispose()
    {
        try { Directory.Delete(_temp, recursive: true); }
        catch { }
    }

    // Invariant: a verb answers from FO4IDE and writes nothing.
    [Fact]
    public void Verbs_report_the_FO4IDE_message_and_write_nothing()
    {
        var obj = Path.Combine(_temp, "quad.obj");
        File.WriteAllText(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        var outNif = Path.Combine(_temp, "quad.nif");

        var import = NifService.Import(obj, outNif, "", "", "", collision: false, fromBlender: false);
        var inspect = NifService.Inspect(obj);

        foreach (var reply in new[] { import, inspect })
        {
            reply.Should().Contain("FO4IDE");
            reply.Should().Contain("no NIF backend");
            reply.Should().NotContain("environment variable");
            reply.Should().NotContain("Build it with");
        }

        File.Exists(outNif).Should().BeFalse("no program may run, so nothing can write the output");

        NifService.GeoBatch(new[] { "Meshes/No/Such/Mesh.nif" });
        NifService.GeoBatchDone.Should().Be(NifService.GeoBatchTotal,
            "a progress poll must reach its total instead of waiting on a verb");
    }
}
