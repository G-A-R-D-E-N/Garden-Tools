using FluentAssertions;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Tests;




public class ConflictStatusTests
{
    [Fact]
    public void Float_formatting_difference_is_not_a_conflict()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "1.0", "1.000000" }, "Text");
        statuses.Should().Equal("master", "identical");
        severity.Should().Be("none");
    }

    [Fact]
    public void Simple_two_way_override()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "10", "20" }, "Text");
        statuses.Should().Equal("master", "win");
        severity.Should().Be("override");
    }

    [Fact]
    public void Three_way_disagreement_is_a_conflict()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "10", "20", "30" }, "Text");
        statuses.Should().Equal("master", "lose", "win");
        severity.Should().Be("conflict");
    }

    [Fact]
    public void Column_matching_winner_but_not_last_is_override()
    {

        var (statuses, _) = MutagenLoader.ClassifyRow(new[] { "10", "20", "20" }, "Text");
        statuses.Should().Equal("master", "override", "win");
    }

    [Fact]
    public void Missing_column_is_notdefined_and_present_plus_missing_differs()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "10", "" }, "Text");
        statuses.Should().Equal("only", "notdefined");
        severity.Should().Be("override");
    }

    [Fact]
    public void Broken_ref_winner_is_critical()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "001234:Base.esp", "Null" }, "Ref");
        statuses.Should().Equal("master", "win");
        severity.Should().Be("critical");
    }

    [Fact]
    public void All_empty_is_notdefined_and_none()
    {
        var (statuses, severity) = MutagenLoader.ClassifyRow(new[] { "", "" }, "Text");
        statuses.Should().Equal("notdefined", "notdefined");
        severity.Should().Be("none");
    }

    [Theory]
    [InlineData("1.0", "1")]
    [InlineData("1.000000", "1")]
    [InlineData("abc", "abc")]
    [InlineData("1e5", "100000")]
    [InlineData("10", "10")]
    [InlineData("20", "20")]
    [InlineData("001234:Base.esp", "001234:Base.esp")]
    public void CanonValue_NormalizesNumbersAndKeepsOtherValuesVerbatim(string input, string expected)
    {
        MutagenLoader.CanonValue(input).Should().Be(expected);
    }

    [Fact]
    public void Override_with_third_agreeing_with_winner_is_override_not_conflict()
    {

        var (_, severity) = MutagenLoader.ClassifyRow(new[] { "10", "20", "20" }, "Text");
        severity.Should().Be("override");
    }
}
