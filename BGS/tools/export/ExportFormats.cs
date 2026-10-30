namespace BehaviourStudio.Tools.Export;

public static class ExportFormats
{
    public const int Version = 1;

    public const string Skeleton = "bgs.skeleton";
    public const string Animation = "bgs.animation";
    public const string BehaviourGraph = "bgs.behaviour-graph";
    public const string Collision = "bgs.collision";
    public const string Schema = "bgs.schema";
    public const string Index = "bgs.export-index";
    public const string Errors = "bgs.export-errors";

    public const string SchemaFile = "schema.json";
    public const string IndexFile = "index.json";
    public const string ErrorFile = "errors.jsonl";

    public const string SkeletonFolder = "skeletons";
    public const string AnimationFolder = "animations";
    public const string GraphFolder = "graphs";
    public const string CollisionFolder = "collision";
}
