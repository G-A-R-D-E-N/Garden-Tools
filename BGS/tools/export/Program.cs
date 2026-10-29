using System;
using System.Collections.Generic;
using System.IO;

namespace BehaviourStudio.Tools.Export;

public static class Program
{
    private const string Help = """
        export, the plain-data exporter for Behaviour Graph Studio.

          export --out <dir> <file>...
              Export every supported file named on the command line. An .hkx writes its
              skeleton, its decodable animation and its behaviour graph when it carries
              them; a .nif writes the collision shapes the native decoder recovers.
              A file that carries none of those is reported as an error row instead of
              being skipped silently.

          export --out <dir> --batch <dir>...
              Walk each directory for .hkx and .nif files and export them, keeping the
              path below the directory in the output name. One failure never stops the
              run: every failure becomes a row in errors.jsonl.

          export --out <dir> --schema-only
              Write schema.json, the document that describes every field of every
              format this tool writes.

        Flags: --out <dir> is required and is created when missing; --batch treats every
        input as a directory to walk; --schema-only writes the schema and exits; --help
        or -h prints this text.

        Output: skeletons/, animations/, graphs/ and collision/ hold one versioned JSON
        document per artifact, index.json lists what this run wrote with input and output
        hashes, and errors.jsonl holds one JSON object per line for every failed input.

        Exit codes: 0 every input exported; 2 when an input failed (its row is in
        errors.jsonl) or when a run's output directory cannot be created (the row is
        reported on the log only, errors.jsonl cannot be written); 1 for usage errors and
        for --schema-only output failures.
        """;

    public static int Main(string[] args) => Run(args, Console.Out, Console.Error);

    public static int Run(string[] args, TextWriter output, TextWriter error)
    {
        ArgumentNullException.ThrowIfNull(args);
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(error);

        string? directory = null;
        bool batch = false;
        bool schemaOnly = false;
        var inputs = new List<string>();

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--help":
                case "-h":
                    output.WriteLine(Help);
                    return 0;

                case "--out":
                    if (++i >= args.Length || args[i].StartsWith("--", StringComparison.Ordinal))
                    {
                        error.WriteLine("--out needs a directory");
                        return 1;
                    }

                    directory = args[i];
                    break;

                case "--batch":
                    batch = true;
                    break;

                case "--schema-only":
                    schemaOnly = true;
                    break;

                default:
                    if (args[i].StartsWith('-'))
                    {
                        error.WriteLine($"unknown option '{args[i]}'");
                        error.WriteLine("Run 'export --help' for the accepted options.");
                        return 1;
                    }

                    inputs.Add(args[i]);
                    break;
            }
        }

        if (string.IsNullOrEmpty(directory))
        {
            error.WriteLine("--out is required");
            output.WriteLine(Help);
            return 1;
        }

        if (schemaOnly && (inputs.Count > 0 || batch))
        {
            error.WriteLine("--schema-only takes no inputs");
            return 1;
        }

        if (!schemaOnly && inputs.Count == 0)
        {
            error.WriteLine("give at least one input file, or --batch with a directory");
            return 1;
        }

        try
        {
            if (schemaOnly)
            {
                ExportRunner.WriteSchema(directory);
                output.WriteLine($"wrote {ExportFormats.SchemaFile}");
                return 0;
            }

            return new ExportRunner(directory, output).Run(inputs, batch);
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException
                                        or InvalidDataException or ArgumentException
                                        or NotSupportedException or System.Security.SecurityException)
        {
            error.WriteLine(failure.Message);
            return 1;
        }
    }
}
