using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using BehaviourStudio.Tools.Export;
using Xunit;

namespace BehaviourStudio.Tests;

// Invariant: the documents the tool writes must be described by its own schema and share one JSON writer.
public sealed class ExportSchemaTests
{
    // Invariant: errors.jsonl must be one line per row and escaped by the document writer, not a second serializer.
    [Fact]
    public void ErrorRowsUseTheDocumentWriterAndStayOnOneLine()
    {
        string directory = ExportTestSupport.Temp("errors-format");
        string input = Path.Combine(directory, "br\u00fcck.hkx");
        File.WriteAllBytes(input, new byte[8]);
        string output = Path.Combine(directory, "out");

        Assert.Equal(2, Program.Run(new[] { "--out", output, input }, TextWriter.Null, TextWriter.Null));

        string line = Assert.Single(File.ReadAllLines(
            Path.Combine(output, ExportFormats.ErrorFile)));
        Assert.Contains("br\\u00fc", line, StringComparison.Ordinal);

        var row = JsonNode.Parse(line)!.AsObject();
        Assert.Equal("br\u00fcck.hkx", row["source"]!.GetValue<string>());
        Assert.NotEmpty(row["message"]!.GetValue<string>());
    }

    // Invariant: schema.json must list every format the tool writes, itself included, with at least one field each.
    [Fact]
    public void SchemaListsItselfAndEveryDocumentFormat()
    {
        string output = ExportTestSupport.Temp("schema");
        Assert.Equal(0, Program.Run(new[] { "--out", output, "--schema-only" }, TextWriter.Null, TextWriter.Null));

        var document = JsonNode.Parse(File.ReadAllText(
            Path.Combine(output, ExportFormats.SchemaFile)))!.AsObject();
        Assert.Equal(ExportFormats.Schema, document["format"]!.GetValue<string>());

        var formats = document["formats"]!.AsArray().Select(format => format!.AsObject()).ToArray();
        var names = formats.Select(format => format["name"]!.GetValue<string>()).ToArray();
        foreach (string expected in new[]
                 {
                     ExportFormats.Skeleton, ExportFormats.Animation, ExportFormats.BehaviourGraph,
                     ExportFormats.Collision, ExportFormats.Index, ExportFormats.Errors,
                     ExportFormats.Schema,
                 })
        {
            Assert.Contains(expected, names);
        }

        Assert.All(formats, format => Assert.NotEmpty(format["fields"]!.AsArray()));
    }
}
