namespace FO4RecordEditor.Services;

public sealed class UndoStack
{
    private readonly Stack<IChange> _undo = new();
    private readonly Stack<IChange> _redo = new();

    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;
    public event Action? Changed;

    public void Do(IChange change)
    {
        change.Apply();
        _undo.Push(change);
        _redo.Clear();
        Changed?.Invoke();
    }

    public void Undo()
    {
        if (!CanUndo) return;
        var c = _undo.Pop();
        c.Undo();
        _redo.Push(c);
        Changed?.Invoke();
    }

    public void Redo()
    {
        if (!CanRedo) return;
        var c = _redo.Pop();
        c.Apply();
        _undo.Push(c);
        Changed?.Invoke();
    }

    public string? PeekUndo() => CanUndo ? _undo.Peek().Description : null;
    public void Clear() { _undo.Clear(); _redo.Clear(); Changed?.Invoke(); }
}
