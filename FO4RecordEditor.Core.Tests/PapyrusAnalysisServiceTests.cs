using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services.Papyrus;

namespace FO4RecordEditor.Core.Tests;

public class PapyrusAnalysisServiceTests
{
    [Theory]
    [InlineData("abc\ndef", 1, 1, 0)]
    [InlineData("abc\ndef", 2, 1, 4)]
    [InlineData("abc\ndef", 2, 3, 6)]
    [InlineData("abc\r\ndef", 2, 1, 5)]
    public void Offset_of_line_and_column(string text, int line, int column, int expected)
    {
        PapyrusAnalysisService.OffsetOf(text, line, column).Should().Be(expected);
    }

    [Theory]
    [InlineData("abc", 0, 1)]
    [InlineData("abc", 1, 0)]
    [InlineData("abc", 5, 1)]
    [InlineData("abc\ndef", 1, 9)]
    public void Out_of_range_positions_are_rejected_rather_than_clamped(string text, int line, int column)
    {
        PapyrusAnalysisService.OffsetOf(text, line, column).Should().Be(-1);
    }

    [Fact]
    public void Check_reports_a_clean_file()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");

        var result = PapyrusAnalysisService.Check(file);
        result.Should().StartWith("RESULT: 1 clean, 0 with syntax errors");
        // The caveat has to survive, or a clean result reads as "this compiles" when it is not.
        result.Should().Contain("does not type-check");
    }

    [Fact]
    public void Check_reports_the_error_position()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\nFunction F(\nEndFunction\n");

        var result = PapyrusAnalysisService.Check(file);
        result.Should().StartWith("RESULT: 0 clean, 1 with syntax errors");
        result.Should().Contain("A.psc(");
    }

    [Fact]
    public void Check_walks_a_folder()
    {
        using var root = new SourceRoot();
        root.Write("A.psc", "ScriptName A\n");
        root.Write("nested/B.psc", "ScriptName nested:B\n");

        PapyrusAnalysisService.Check(root.Path).Should().StartWith("RESULT: 2 clean, 0 with syntax errors");
    }

    [Fact]
    public void Check_refuses_a_pex_with_guidance()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.pex", "not really a pex");

        PapyrusAnalysisService.Check(file).Should().Contain("decompile_papyrus");
    }

    [Fact]
    public void Check_reports_a_missing_path_rather_than_throwing()
    {
        PapyrusAnalysisService.Check(Path.Combine(Path.GetTempPath(), "fo4re-no-such-file.psc"))
            .Should().Contain("Not found");
    }

    [Fact]
    public void Outline_lists_declarations_with_positions()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\nint Property Health auto\nFunction F()\nEndFunction\n");

        var result = PapyrusAnalysisService.Outline(file);
        result.Should().Contain("Property");
        result.Should().Contain("int Property Health Auto");
        result.Should().Contain("Function F()");
    }

    [Fact]
    public void Outline_still_works_on_a_file_with_errors_and_says_so()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\nint Property Health auto\n%%%\n");

        var result = PapyrusAnalysisService.Outline(file);
        result.Should().Contain("syntax error");
        result.Should().Contain("Health");
    }

    [Fact]
    public void Definition_resolves_across_files_through_extends()
    {
        using var root = new SourceRoot();
        root.Write("Parent.psc", "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
        var child = root.Write("Child.psc", "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n");

        // Line 3, column 3 is the 'D' of DoStuff.
        var result = PapyrusAnalysisService.Definition(child, 3, 3);

        result.Should().Contain("RESULT: Function DoStuff");
        result.Should().Contain("in: Parent");
        result.Should().Contain("Parent.psc(2,10)");
        result.Should().Contain("Parent version");
    }

    [Fact]
    public void Definition_says_plainly_when_it_cannot_resolve()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n");

        var result = PapyrusAnalysisService.Definition(file, 3, 15);
        result.Should().StartWith("RESULT: not resolved.");
        result.Should().Contain("no type checker");
    }

    [Fact]
    public void Definition_rejects_a_position_outside_the_file()
    {
        using var root = new SourceRoot();
        var file = root.Write("A.psc", "ScriptName A\n");

        PapyrusAnalysisService.Definition(file, 99, 1).Should().Contain("outside");
    }
}
