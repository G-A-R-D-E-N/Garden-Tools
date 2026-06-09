using System.IO;
using System.Text.Json;
using FO4RecordEditor.Models;
using YamlDotNet.RepresentationModel;

namespace FO4RecordEditor.Services;

public class SpriggitLoader
{
    public static RecordNode LoadFolder(string folderPath)
    {
        var root = new RecordNode { Key = Path.GetFileName(folderPath), FilePath = folderPath };
        foreach (var dir in Directory.GetDirectories(folderPath).OrderBy(d => d))
        {
            var child = LoadFolder(dir);
            child.Parent = root;
            root.Children.Add(child);
        }
        foreach (var file in Directory.GetFiles(folderPath, "*.*")
                     .Where(f => f.EndsWith(".yaml", StringComparison.OrdinalIgnoreCase)
                              || f.EndsWith(".yml",  StringComparison.OrdinalIgnoreCase)
                              || f.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                     .OrderBy(f => f))
        {
            try
            {
                var fileNode = LoadFile(file);
                fileNode.Parent = root;
                root.Children.Add(fileNode);
            }
            catch (Exception ex)
            {
                var errNode = new RecordNode { Key = Path.GetFileName(file), FilePath = file };
                errNode.Children.Add(new RecordNode { Key = "LoadError", Value = ex.Message });
                errNode.Parent = root;
                root.Children.Add(errNode);
            }
        }
        return root;
    }

    public static RecordNode LoadFile(string filePath)
    {
        var ext = Path.GetExtension(filePath).ToLowerInvariant();
        var root = new RecordNode { Key = Path.GetFileName(filePath), FilePath = filePath };

        if (ext is ".yaml" or ".yml")
            LoadYaml(filePath, root);
        else if (ext == ".json")
            LoadJson(filePath, root);

        return root;
    }

    // ---- YAML -----------------------------------------------------------

    private static void LoadYaml(string filePath, RecordNode root)
    {
        var yaml = new YamlStream();
        using var reader = new StreamReader(filePath);
        yaml.Load(reader);

        if (yaml.Documents.Count == 0) return;
        var doc = yaml.Documents[0].RootNode;
        PopulateFromYamlNode(doc, root, filePath);
    }

    private static void PopulateFromYamlNode(YamlNode yamlNode, RecordNode parent, string filePath)
    {
        switch (yamlNode)
        {
            case YamlMappingNode map:
                foreach (var entry in map.Children)
                {
                    var key = entry.Key.ToString();
                    var child = new RecordNode { Key = key, FilePath = filePath, Parent = parent };
                    PopulateFromYamlNode(entry.Value, child, filePath);
                    parent.Children.Add(child);
                }
                break;

            case YamlSequenceNode seq:
                for (int i = 0; i < seq.Children.Count; i++)
                {
                    var child = new RecordNode { Key = $"[{i}]", FilePath = filePath, Parent = parent };
                    PopulateFromYamlNode(seq.Children[i], child, filePath);
                    parent.Children.Add(child);
                }
                break;

            case YamlScalarNode scalar:
                parent.Value = scalar.Value ?? "";
                break;
        }
    }

    // ---- JSON -----------------------------------------------------------

    private static void LoadJson(string filePath, RecordNode root)
    {
        var text = File.ReadAllText(filePath);
        using var doc = JsonDocument.Parse(text);
        PopulateFromJsonElement(doc.RootElement, root, filePath);
    }

    private static void PopulateFromJsonElement(JsonElement el, RecordNode parent, string filePath)
    {
        switch (el.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var prop in el.EnumerateObject())
                {
                    var child = new RecordNode { Key = prop.Name, FilePath = filePath, Parent = parent };
                    PopulateFromJsonElement(prop.Value, child, filePath);
                    parent.Children.Add(child);
                }
                break;

            case JsonValueKind.Array:
                int i = 0;
                foreach (var item in el.EnumerateArray())
                {
                    var child = new RecordNode { Key = $"[{i++}]", FilePath = filePath, Parent = parent };
                    PopulateFromJsonElement(item, child, filePath);
                    parent.Children.Add(child);
                }
                break;

            default:
                parent.Value = el.ToString();
                break;
        }
    }

    // ---- Save back to disk ----------------------------------------------

    public static void SaveFile(RecordNode fileNode)
    {
        if (fileNode.FilePath == null) return;
        var ext = Path.GetExtension(fileNode.FilePath).ToLowerInvariant();

        if (ext is ".yaml" or ".yml")
            SaveYaml(fileNode);
        else if (ext == ".json")
            SaveJson(fileNode);

        fileNode.ClearDirty();
    }

    private static void SaveYaml(RecordNode root)
    {
        var mapping = (YamlMappingNode)BuildYamlNode(root);
        var stream = new YamlStream(new YamlDocument(mapping));
        using var writer = new StreamWriter(root.FilePath!, false, System.Text.Encoding.UTF8);
        stream.Save(writer, false);
    }

    private static YamlNode BuildYamlNode(RecordNode node)
    {
        if (node.IsLeaf)
            return new YamlScalarNode(node.Value);

        // If all children have numeric bracket keys -> sequence
        if (node.Children.All(c => c.Key.StartsWith('[') && c.Key.EndsWith(']')))
        {
            var seq = new YamlSequenceNode();
            foreach (var c in node.Children)
                seq.Add(BuildYamlNode(c));
            return seq;
        }

        var map = new YamlMappingNode();
        foreach (var c in node.Children)
            map.Add(c.Key, BuildYamlNode(c));
        return map;
    }

    private static void SaveJson(RecordNode root)
    {
        var opts = new JsonWriterOptions { Indented = true };
        using var ms = new System.IO.MemoryStream();
        using (var writer = new Utf8JsonWriter(ms, opts))
            WriteJsonNode(writer, root);
        File.WriteAllBytes(root.FilePath!, ms.ToArray());
    }

    private static void WriteJsonNode(Utf8JsonWriter w, RecordNode node)
    {
        if (node.IsLeaf) { w.WriteStringValue(node.Value); return; }

        bool isArray = node.Children.All(c => c.Key.StartsWith('[') && c.Key.EndsWith(']'));
        if (isArray)
        {
            w.WriteStartArray();
            foreach (var c in node.Children) WriteJsonNode(w, c);
            w.WriteEndArray();
        }
        else
        {
            w.WriteStartObject();
            foreach (var c in node.Children)
            {
                w.WritePropertyName(c.Key);
                WriteJsonNode(w, c);
            }
            w.WriteEndObject();
        }
    }
}
