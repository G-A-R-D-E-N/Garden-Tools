using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools.Export;

public static class CollisionExport
{
    public static string Json(NifCollisionModel model, string source, int block, string type)
    {
        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.Collision)
            .Name("version").Number(ExportFormats.Version)
            .Name("source").Text(source)
            .Name("block").Number(block)
            .Name("type").Text(type)
            .Name("sha256").Text(model.Sha256Hex())
            .Name("pointerSize").Number(model.PointerSize)
            .Name("lengthPrefixed").Boolean(model.LengthPrefixed)
            .Name("systemClass").Text(model.SystemClass)
            .Name("bodies").Array();

        foreach (var body in model.Bodies)
        {
            json.Object()
                .Name("index").Number(body.Index)
                .Name("shape").Number(body.Shape)
                .Name("materialId").Number(body.MaterialId)
                .Name("materialHash").Number(body.MaterialHash)
                .Name("flags").Number(body.Flags)
                .Name("position").Array();
            foreach (float value in body.Position) json.Number(value);

            json.EndArray().Name("orientation").Array();
            foreach (float value in body.Orientation) json.Number(value);

            json.EndArray().Name("response").Object()
                .Name("dynamicFriction").Number(body.Response.DynamicFriction)
                .Name("staticFriction").Number(body.Response.StaticFriction)
                .Name("restitution").Number(body.Response.Restitution)
                .Name("frictionCombine").Number(body.Response.FrictionCombine)
                .Name("restitutionCombine").Number(body.Response.RestitutionCombine)
                .EndObject()
                .EndObject();
        }

        json.EndArray().Name("shapes").Array();

        foreach (var shape in model.Shapes)
        {
            json.Object()
                .Name("index").Number(shape.Index)
                .Name("class").Text(shape.Class)
                .Name("kind").Text(Kind(shape.Kind))
                .Name("convexRadius").Number(shape.ConvexRadius)
                .Name("vertexCount").Number(shape.VertexCount)
                .Name("vertices").Array();
            foreach (float value in shape.Vertices) json.Number(value);

            json.EndArray().Name("triangles").Array();
            foreach (int value in shape.Triangles) json.Number(value);

            json.EndArray().Name("instances").Array();
            foreach (var instance in shape.Instances)
            {
                json.Object().Name("shape").Number(instance.Shape).Name("transform").Array();
                foreach (float value in instance.Transform) json.Number(value);

                json.EndArray().Name("scale").Array();
                foreach (float value in instance.Scale) json.Number(value);

                json.EndArray().EndObject();
            }

            json.EndArray().EndObject();
        }

        json.EndArray().Name("notes").Array();
        foreach (string note in model.Notes) json.Text(note);
        json.EndArray();

        return json.EndObject().Finish();
    }

    private static string Kind(NifCollisionShapeKind kind) => kind switch
    {
        NifCollisionShapeKind.Sphere => "sphere",
        NifCollisionShapeKind.ConvexPolytope => "convexPolytope",
        NifCollisionShapeKind.Compound => "compound",
        NifCollisionShapeKind.CompressedMesh => "compressedMesh",
        _ => "unknown",
    };
}
