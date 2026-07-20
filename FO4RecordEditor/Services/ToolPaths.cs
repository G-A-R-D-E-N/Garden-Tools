using System.IO;
using Microsoft.Win32;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

/// <summary>
/// Resolves the external executables and script roots the tool shells out to (niftool, the Creation
/// Kit Papyrus compiler, texconv) on a machine that is not the developer's. Every lookup runs the
/// same chain: environment override, then the user's settings.json, then a copy bundled next to the
/// exe, then the local Fallout 4 / Creation Kit install, and finally the original dev-box paths.
/// The chain is ordered so an explicit choice always beats an auto-detected one.
/// </summary>
public static class ToolPaths
{
    private static AppSettings? _settings;

    private static AppSettings Settings
    {
        get
        {
            if (_settings != null) return _settings;
            try
            {
                var file = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "FO4RecordEditor", "settings.json");
                _settings = File.Exists(file)
                    ? JsonConvert.DeserializeObject<AppSettings>(File.ReadAllText(file)) ?? new AppSettings()
                    : new AppSettings();
            }
            catch { _settings = new AppSettings(); }
            return _settings;
        }
    }

    /// <summary>Drop the cached settings so a path edited in the GUI takes effect without a restart.</summary>
    public static void Invalidate() => _settings = null;

    private static string App(params string[] parts) =>
        Path.Combine(new[] { AppContext.BaseDirectory }.Concat(parts).ToArray());

    private static string? FirstFile(params string?[] candidates) =>
        candidates.FirstOrDefault(p => !string.IsNullOrWhiteSpace(p) && File.Exists(p));

    private static string? FirstDir(params string?[] candidates) =>
        candidates.FirstOrDefault(p => !string.IsNullOrWhiteSpace(p) && Directory.Exists(p));

    private static string? Env(string name)
    {
        var v = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(v) ? null : v.Trim().Trim('"');
    }

    /// <summary>
    /// The Fallout 4 install root, from settings, then the registry key the game's installer writes,
    /// then the default Steam library location. Null when Fallout 4 is not installed.
    /// </summary>
    public static string? Fallout4Root()
    {
        var s = Settings.Fallout4Path;
        if (!string.IsNullOrWhiteSpace(s) && Directory.Exists(s)) return s;

        foreach (var key in new[]
                 {
                     @"SOFTWARE\WOW6432Node\Bethesda Softworks\Fallout4",
                     @"SOFTWARE\Bethesda Softworks\Fallout4",
                 })
        {
            try
            {
                using var k = Registry.LocalMachine.OpenSubKey(key);
                var path = k?.GetValue("installed path") as string;
                if (!string.IsNullOrWhiteSpace(path) && Directory.Exists(path)) return path;
            }
            catch { /* registry unreadable — fall through to the disk probe */ }
        }

        foreach (var drive in DriveInfo.GetDrives())
        {
            foreach (var rel in new[]
                     {
                         @"SteamLibrary\steamapps\common\Fallout 4",
                         @"Program Files (x86)\Steam\steamapps\common\Fallout 4",
                     })
            {
                try
                {
                    var probe = Path.Combine(drive.Name, rel);
                    if (Directory.Exists(probe)) return probe;
                }
                catch { /* drive not ready */ }
            }
        }
        return null;
    }

    /// <summary>niftool.exe — our nifly-based CLI. Bundled in the release under tools\niftool\.</summary>
    public static string? Niftool() => FirstFile(
        Env("NIFTOOL_PATH"),
        Settings.NiftoolPath,
        App("tools", "niftool", "niftool.exe"),
        App("niftool", "niftool.exe"),
        @"E:\F4SE OG\Tools\PluginEditTool\niftool\build\windows\x64\release\niftool.exe");

    /// <summary>texconv — ships inside xEdit's Edit Scripts folder; also bundled in the release.</summary>
    public static string? Texconv() => FirstFile(
        Env("TEXCONV_PATH"),
        Settings.TexconvPath,
        App("tools", "texconv", "Texconvx64.exe"),
        App("tools", "texconv", "Texconv.exe"),
        @"E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\TES5Edit-dev-4.1.6\Build\Edit Scripts\Texconvx64.exe",
        @"E:\F4SE OG\Tools\PluginEditTool\FO4RecordEditor\TES5Edit-dev-4.1.6\Build\Edit Scripts\Texconv.exe");

    /// <summary>
    /// The Creation Kit's PapyrusCompiler.exe. It is not redistributable, so it is never bundled —
    /// it is found in the user's own Fallout 4 install.
    /// </summary>
    public static string? PapyrusCompiler()
    {
        var root = Fallout4Root();
        return FirstFile(
            Env("PAPYRUS_COMPILER_PATH"),
            Settings.PapyrusCompilerPath,
            root == null ? null : Path.Combine(root, "Papyrus Compiler", "PapyrusCompiler.exe"),
            @"E:\SteamLibrary\steamapps\common\Fallout 4 1946160\Papyrus Compiler\PapyrusCompiler.exe",
            @"E:\F4SE OG\Tools\PapyrusCompiler\Papyrus Compiler\PapyrusCompiler.exe");
    }

    /// <summary>
    /// Every base-script source root that exists, highest priority first. F4SE must precede the
    /// vanilla base: its Game.psc/Actor.psc declare extra natives that the vanilla ones lack.
    /// </summary>
    public static List<string> PapyrusBaseImports()
    {
        var roots = new List<string>();

        void Add(string? dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) return;
            dir = dir.Trim().Trim('"');
            if (Directory.Exists(dir) && !roots.Contains(dir, StringComparer.OrdinalIgnoreCase))
                roots.Add(dir);
        }

        foreach (var p in (Env("PAPYRUS_BASE_IMPORTS") ?? "").Split(';', StringSplitOptions.RemoveEmptyEntries))
            Add(p);
        foreach (var p in (Settings.PapyrusBaseImports ?? "").Split(';', StringSplitOptions.RemoveEmptyEntries))
            Add(p);

        Add(App("tools", "papyrus", "F4SE"));
        Add(App("tools", "papyrus", "Base"));

        var root = Fallout4Root();
        if (root != null)
        {
            Add(Path.Combine(root, "Data", "Scripts", "Source", "User"));
            Add(Path.Combine(root, "Data", "Scripts", "Source", "Base"));
        }

        Add(@"E:\F4SE OG\Tools\f4se_0_06_23\Data\Scripts\Source");
        Add(@"E:\F4SE OG\Tools\Scripting Resources\BaaseGameScripts\Base");
        Add(@"E:\F4SE OG\Tools\PluginEditTool\papyrus\Base");
        return roots;
    }

    /// <summary>
    /// The offline Creation Kit Wiki HTML mirror root that papyrus_function_lookup / papyrus_script_info
    /// read from. Ships bundled with the release package (package.ps1) under tools\ckwiki\fallout4\, so
    /// this resolves out of the box with no launch flag or Settings edit needed; CkWikiPath/CK_WIKI_PATH
    /// only matter if you want to point at a different or newer mirror.
    /// </summary>
    public static string? CkWiki() => FirstDir(
        Env("CK_WIKI_PATH"),
        Settings.CkWikiPath,
        App("tools", "ckwiki", "fallout4"),
        @"E:\F4SE OG\docs\Knowledge Materials\Creation Kit Wiki\Fallout 4 Creation Kit Wiki-52183-21-05-20-1621512739\FO4CKWiki_210520\fallout4");

    /// <summary>
    /// ffmpeg — decodes any input audio (mp3/flac/ogg/m4a/wma/...) to PCM WAV for xWMAEncode, and the
    /// reverse (xwm-derived WAV to whatever output format is asked for). Bundled under tools\audio\.
    /// </summary>
    public static string? Ffmpeg() => FirstFile(
        Env("FFMPEG_PATH"),
        Settings.FfmpegPath,
        App("tools", "audio", "ffmpeg.exe"),
        @"E:\F4SE OG\Tools\Audio Converter\bin\ffmpeg.exe");

    /// <summary>Microsoft's xWMAEncode.exe — the only thing that actually reads/writes xWMA. Encodes
    /// PCM WAV to xWMA and decodes xWMA back to PCM WAV depending on which direction the input is.
    /// Bundled under tools\audio\.</summary>
    public static string? XwmaEncode() => FirstFile(
        Env("XWMAENCODE_PATH"),
        Settings.XwmaEncodePath,
        App("tools", "audio", "xWMAEncode.exe"),
        @"E:\F4SE OG\Tools\Audio Converter\bin\xWMAEncode.exe");

    /// <summary>BmlFuzEncode.exe (BowmoreLover) — packs an xwm + a lip file into a .fuz voice
    /// container. Bundled-only: single-purpose, nobody keeps their own copy to point at.</summary>
    public static string? BmlFuzEncode() => FirstFile(
        Env("BMLFUZENCODE_PATH"),
        App("tools", "audio", "BmlFuzEncode.exe"),
        @"E:\F4SE OG\Tools\Audio Converter\bin\BmlFuzEncode.exe");

    /// <summary>BmlFuzDecode.exe (BowmoreLover) — splits a .fuz voice container back into its xwm and
    /// lip file. Bundled-only, same reasoning as BmlFuzEncode.</summary>
    public static string? BmlFuzDecode() => FirstFile(
        Env("BMLFUZDECODE_PATH"),
        App("tools", "audio", "BmlFuzDecode.exe"),
        @"E:\F4SE OG\Tools\Audio Converter\bin\BmlFuzDecode.exe");

    /// <summary>A one-line "here is where I looked" for the not-found errors the AI reads back.</summary>
    public static string Describe(string tool) => tool switch
    {
        "niftool" => "set NIFTOOL_PATH, or put niftool.exe in tools\\niftool\\ next to the exe",
        "texconv" => "set TEXCONV_PATH, or put Texconvx64.exe in tools\\texconv\\ next to the exe",
        "papyrus" => "install the Creation Kit, or set PAPYRUS_COMPILER_PATH / papyrusCompilerPath in settings.json",
        "ckwiki" => "set CK_WIKI_PATH, or put the wiki mirror in tools\\ckwiki\\fallout4\\ next to the exe",
        "ffmpeg" => "set FFMPEG_PATH, or put ffmpeg.exe in tools\\audio\\ next to the exe",
        "xwmaencode" => "set XWMAENCODE_PATH, or put xWMAEncode.exe in tools\\audio\\ next to the exe",
        "bmlfuzencode" => "put BmlFuzEncode.exe in tools\\audio\\ next to the exe",
        "bmlfuzdecode" => "put BmlFuzDecode.exe in tools\\audio\\ next to the exe",
        _ => "",
    };
}
