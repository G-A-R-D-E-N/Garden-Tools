namespace BehaviourStudio.App;

public sealed record AssistantModelItem(string WireModel, string Label)
{
    public override string ToString() => Label;
}

public sealed record AssistantProviderItem(string Wire, string Label)
{
    public override string ToString() => Label;
}
