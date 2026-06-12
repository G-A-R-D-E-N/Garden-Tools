using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using FO4RecordEditor.Models;
using Newtonsoft.Json;

namespace FO4RecordEditor.Services;

[ClassInterface(ClassInterfaceType.AutoDual)]
[ComVisible(true)]
public class BackendInterop
{
    private readonly IEnumerable<ConflictEntry> _conflicts;
    private readonly object? _env;

    public BackendInterop(IEnumerable<ConflictEntry> conflicts, object? env)
    {
        _conflicts = conflicts;
        _env = env;
    }

    public string GetConflicts()
    {
        return JsonConvert.SerializeObject(_conflicts);
    }

    public string GetConflictMatrix(string formKey)
    {
        var matrix = MutagenLoader.BuildConflictMatrix(_env, formKey);
        return JsonConvert.SerializeObject(matrix);
    }
    
    public string ResolveConflict(string formKey, string winner, string patch)
    {
        return WriteService.ResolveConflict(_env, formKey, winner, patch);
    }

    public string SavePatch(string patch)
    {
        return WriteService.SavePlugin(patch, null);
    }

    public string[] GetEditablePlugins()
    {
        return WriteService.EditablePlugins().ToArray();
    }
}
