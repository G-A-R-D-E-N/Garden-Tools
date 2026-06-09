namespace FO4RecordEditor.Services;
public interface IChange { string Description { get; } void Apply(); void Undo(); }
