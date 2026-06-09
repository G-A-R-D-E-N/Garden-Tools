using FluentAssertions;
using FO4RecordEditor.Models;
using FO4RecordEditor.Services;
using FO4RecordEditor.Tests.Helpers;
using Xunit;

namespace FO4RecordEditor.Tests;

public class ErrorScannerTests
{
    [Fact]
    public void Scan_FlagsNullActorValue()
    {
        var rec = GraphTestBuilder.Record("COBJ", "001001:Test.esp", "MyRecipe",
            ("ActorValue", "NULL - Null Reference [00000000]"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().Contain(e => e.Category == ErrorCategory.NullActorValue
                                  && e.FormKey == "001001:Test.esp");
    }

    [Fact]
    public void Scan_FlagsBrokenReference()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Keyword1", "00BEEF:Missing.esp"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().Contain(e => e.Category == ErrorCategory.BrokenReference
                                  && e.Description.Contains("00BEEF:Missing.esp"));
    }

    [Fact]
    public void Scan_DoesNotFlagVanillaReference()
    {
        var rec = GraphTestBuilder.Record("WEAP", "001001:Test.esp", "MyGun",
            ("Keyword1", "0004A0:Fallout4.esm"));
        var g = new KnowledgeGraph();
        g.Index(GraphTestBuilder.Plugin("Test.esp", rec));

        var errors = new ErrorScanner().Scan(g);
        errors.Should().NotContain(e => e.Category == ErrorCategory.BrokenReference);
    }
}
