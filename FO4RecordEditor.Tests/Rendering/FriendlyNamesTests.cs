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

    [Fact]
    public void LabelPath_relabels_plain_names_but_leaves_paths_and_indices_intact()
    {
        FriendlyNames.LabelPath("MarkerColor").Should().Be("Marker Color");
        FriendlyNames.LabelPath("Components[0].Component").Should().Be("Components[0].Component");
        FriendlyNames.LabelPath("[0]").Should().Be("[0]");
    }
}
