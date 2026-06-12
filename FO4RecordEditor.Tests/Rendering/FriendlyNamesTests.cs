using FluentAssertions;
using FO4RecordEditor.Services.Rendering;
using Xunit;

namespace FO4RecordEditor.Tests.Rendering;

public class FriendlyNamesTests
{
    [Fact]
    public void Maps_known_names_and_passes_through_unknowns()
    {
        FriendlyNames.Label("EditorID").Should().Be("Editor ID");
        FriendlyNames.Label("CreatedObject").Should().Be("Created Object");
        FriendlyNames.Label("WorkbenchKeyword").Should().Be("Workbench Keyword");
        FriendlyNames.Label("SomeUnmappedField").Should().Be("SomeUnmappedField");
        FriendlyNames.Label("[0]").Should().Be("[0]");
    }
}
