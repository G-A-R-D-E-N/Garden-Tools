namespace FO4RecordEditor.Services;

public static class EnvironmentState
{
    private static volatile EnvironmentSnapshot? _current;

    public static EnvironmentSnapshot? Current => _current;

    internal static void Publish(EnvironmentSnapshot snapshot) => _current = snapshot;
}
