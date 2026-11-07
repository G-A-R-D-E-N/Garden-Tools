using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services;

namespace BehaviourStudio.App;

public partial class MainWindow : Window
{
    private async Task ImportGltfAnimation()
    {
        string? gltf = await PickImportFile("Which glTF or GLB animation to import", "glTF animations",
                                            "*.gltf", "*.glb");
        if (gltf == null) return;

        string? skeleton = await PickImportFile("Which skeleton .hkx the glTF bones match", "Havok files", "*.hkx");
        if (skeleton == null) return;

        string? template = await PickImportFile("Which animation .hkx to write the clip through", "Havok files",
                                                "*.hkx");
        if (template == null) return;

        await ReviewGltfImport(gltf, skeleton, template);
    }

    private async Task<string?> PickImportFile(string title, string label, params string[] patterns)
    {
        var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = title,
            AllowMultiple = false,
            SuggestedStartLocation = await StartFolder(),
            FileTypeFilter = new[]
            {
                new FilePickerFileType(label) { Patterns = patterns },
                FilePickerFileTypes.All,
            },
        });

        return picked.Count > 0 ? picked[0].TryGetLocalPath() : null;
    }

    private async Task ReviewGltfImport(string gltfPath, string skeletonPath, string templatePath)
    {
        var answer = new TextBlock
        {
            Foreground = Ux.MetaBrush,
            FontSize = 12,
            TextWrapping = TextWrapping.Wrap,
        };
        var names = new ComboBox { MinWidth = 260, IsVisible = false };
        var preview = Ux.Secondary("Preview");
        var save = Ux.Primary("Save...");
        var close = Ux.Secondary("Close");
        save.IsEnabled = false;
        byte[]? written = null;

        try
        {
            var clips = GltfScene.Read(gltfPath).Animations.Select(clip => clip.Name).ToList();
            names.ItemsSource = clips;
            names.IsVisible = clips.Count > 1;
        }
        catch (Exception error) when (IsImportFailure(error))
        {
            answer.Text = "Not written: " + error.Message;
            answer.Foreground = Ux.BadBrush;
        }

        void RunPreview()
        {
            written = null;
            save.IsEnabled = false;
            try
            {
                var skeleton = new HkxBinaryReader().ReadSkeleton(skeletonPath);
                var imported = GltfAnimationImport.Build(GltfScene.Read(gltfPath), skeleton,
                                                         names.SelectedItem as string);
                var result = GltfAnimationImport.Write(InputFilePolicy.ReadHkx(templatePath), imported, skeleton);
                written = result.Bytes;
                answer.Text = GltfImportSummary(imported, result);
                answer.Foreground = Ux.MetaBrush;
                save.IsEnabled = true;
            }
            catch (Exception error) when (IsImportFailure(error))
            {
                answer.Text = "Not written: " + error.Message;
                answer.Foreground = Ux.BadBrush;
            }
        }

        var window = new Window
        {
            Title = "Import glTF animation",
            Width = 640,
            Height = 480,
            Background = Ux.BaseBrush,
            Content = new StackPanel
            {
                Margin = new Thickness(20),
                Spacing = 10,
                Children =
                {
                    Ux.SectionTitle("Import glTF animation"),
                    new TextBlock
                    {
                        Text = $"glTF: {gltfPath}\nSkeleton: {skeletonPath}\nTemplate: {templatePath}",
                        Foreground = Ux.MutedBrush,
                        FontSize = 11,
                        TextWrapping = TextWrapping.Wrap,
                    },
                    names,
                    new ScrollViewer { Height = 250, Content = answer },
                    Ux.Wrap(preview, save, close),
                },
            },
        };

        preview.Click += (_, _) => RunPreview();
        names.SelectionChanged += (_, _) => RunPreview();
        close.Click += (_, _) => window.Close();
        save.Click += async (_, _) =>
        {
            if (written is not { } bytes) return;
            string? path = await PickImportOutput(gltfPath);
            if (path == null) return;

            if (new[] { gltfPath, skeletonPath, templatePath }.Any(input =>
                    string.Equals(Path.GetFullPath(input), Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase)))
            {
                answer.Text = $"Not written: {path} is one of the inputs.";
                answer.Foreground = Ux.BadBrush;
                return;
            }

            try
            {
                FileSafety.Replace(path, bytes);
                answer.Text = $"Saved {bytes.Length} bytes to {path}.";
                answer.Foreground = Ux.MetaBrush;
                SetStatus($"Imported the glTF animation to {Path.GetFileName(path)}.", Ux.MetaBrush);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                answer.Text = "Not written: " + error.Message;
                answer.Foreground = Ux.BadBrush;
            }
        };

        RunPreview();
        await window.ShowDialog(this);
    }

    private async Task<string?> PickImportOutput(string gltfPath)
    {
        var picked = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Save the imported animation as",
            SuggestedFileName = Path.GetFileNameWithoutExtension(gltfPath) + ".hkx",
            SuggestedStartLocation = await StartFolder(),
            DefaultExtension = "hkx",
            FileTypeChoices = new[]
            {
                new FilePickerFileType("Havok animation") { Patterns = new[] { "*.hkx" } },
            },
        });

        return picked?.TryGetLocalPath();
    }

    private static string GltfImportSummary(GltfAnimationImport.Result imported, GltfAnimationImport.Written written)
    {
        var report = imported.Report;
        return $"{report.Frames} frame(s) at {report.FrameRate:F0} per second, {report.Duration:F3} s, " +
               $"{imported.Animation.Tracks.Count} track(s)\n" +
               $"Animated bones: {report.Animated.Count}. Reference pose only: {report.ReferenceOnly.Count}. " +
               $"Joints the skeleton does not name, ignored: {report.Unmapped.Count}.\n" +
               (report.RestMismatch.Count == 0
                   ? ""
                   : $"Rest pose differs from the skeleton's: {string.Join(", ", report.RestMismatch)}.\n") +
               $"Root motion from the root bone: {imported.Motion}.\n" +
               $"Template annotations cleared: {written.AnnotationsCleared}. " +
               $"glTF annotations written: {written.AnnotationsWritten}.\n" +
               $"Preview is ready: {written.Bytes.Length} bytes, worst error {written.PositionError:F4} units, " +
               $"{written.RotationError:F5} radians, {written.ScaleError:F5} scale. Save to keep it.";
    }

    private static bool IsImportFailure(Exception error) =>
        error is InvalidDataException or InvalidOperationException or NotSupportedException or FormatException or
                 IOException or UnauthorizedAccessException or ArgumentException;
}
