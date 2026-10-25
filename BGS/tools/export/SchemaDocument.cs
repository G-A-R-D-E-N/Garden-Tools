namespace BehaviourStudio.Tools.Export;

public static class SchemaDocument
{
    private sealed record Field(string Path, string Type, string Notes);

    private sealed record Format(string Name, string Summary, Field[] Fields);

    public static string Json()
    {
        var formats = new[]
        {
            new Format(ExportFormats.Skeleton, "Bones, parents and the reference pose of one hkaSkeleton.", new[]
            {
                new Field("format", "string", "always " + ExportFormats.Skeleton),
                new Field("version", "integer", "format version, currently 1"),
                new Field("source", "string", "input path as given, relative to the batch root"),
                new Field("name", "string", "hkaSkeleton name"),
                new Field("boneCount", "integer", "number of bones; equal to the length of bones"),
                new Field("bones", "array", "one entry per bone, in skeleton order"),
                new Field("bones[].index", "integer", "bone index"),
                new Field("bones[].name", "string", "bone name as stored in the file"),
                new Field("bones[].parent", "integer|null", "parent bone index, -1 for a root, null when the file holds fewer parents than bones"),
                new Field("referencePose", "array", "reference pose entries as decoded, in file order"),
                new Field("referencePose[].translation", "number[3]", "x, y, z in Havok units, unconverted"),
                new Field("referencePose[].rotation", "number[4]", "quaternion as x, y, z, w"),
                new Field("referencePose[].scale", "number[3]", "x, y, z scale"),
                new Field("notes", "string[]", "conditions the decoder met while reading, empty when none"),
            }),
            new Format(ExportFormats.Animation, "Per-frame TRS for every track, with the spline already decoded, plus events and binding.", new[]
            {
                new Field("format", "string", "always " + ExportFormats.Animation),
                new Field("version", "integer", "format version, currently 1"),
                new Field("source", "string", "input path as given, relative to the batch root"),
                new Field("class", "string", "decoded animation class, one of HkxAnimationData.DecodedAnimationClasses"),
                new Field("duration", "number", "clip duration in seconds as stored"),
                new Field("numFrames", "integer", "frames decoded per track"),
                new Field("numTracks", "integer", "transform tracks; equal to the length of tracks"),
                new Field("frameDuration", "number", "seconds per frame as stored"),
                new Field("skeletonName", "string", "skeleton named by the animation binding, empty when there is none"),
                new Field("blendHint", "integer", "hkaAnimationBinding blend hint"),
                new Field("blocks", "object", "compression block geometry as stored"),
                new Field("blocks.numBlocks", "integer", "spline blocks"),
                new Field("blocks.maxFramesPerBlock", "integer", "frames per spline block"),
                new Field("blocks.blockDuration", "number", "seconds per spline block"),
                new Field("boneNames", "string[]", "bone names carried by the animation"),
                new Field("trackToBone", "integer[]", "track to bone index mapping; may be shorter than tracks"),
                new Field("events", "array", "annotation events from hkaAnnotationTrack"),
                new Field("events[].time", "number", "event time in seconds"),
                new Field("events[].text", "string", "event text"),
                new Field("tracks", "array", "one entry per transform track"),
                new Field("tracks[].index", "integer", "track index"),
                new Field("tracks[].bone", "integer|null", "bone this track drives, null when the mapping is shorter than the track list"),
                new Field("tracks[].frames", "array", "numFrames entries, in time order"),
                new Field("tracks[].frames[].translation", "number[3]", "x, y, z in Havok units, unconverted"),
                new Field("tracks[].frames[].rotation", "number[4]", "quaternion as x, y, z, w, not renormalised"),
                new Field("tracks[].frames[].scale", "number[3]", "x, y, z scale"),
                new Field("notes", "string[]", "conditions the decoder met while reading, empty when none"),
            }),
            new Format(ExportFormats.BehaviourGraph, "State machines, their states, generators, transitions and the gaps a consumer would leave.", new[]
            {
                new Field("format", "string", "always " + ExportFormats.BehaviourGraph),
                new Field("version", "integer", "format version, currently 1"),
                new Field("source", "string", "input path as given, relative to the batch root"),
                new Field("graphName", "string", "hkbBehaviorGraph name, empty when the file holds no graph object"),
                new Field("rootMachine", "integer|null", "index into machines for the entry point, null when none resolved"),
                new Field("machines", "array", "state machines in discovery order, root first"),
                new Field("machines[].index", "integer", "index into machines"),
                new Field("machines[].name", "string", "machine name"),
                new Field("machines[].startStateId", "integer", "start state id as stored"),
                new Field("machines[].states", "array", "states in graph order"),
                new Field("machines[].states[].stateId", "integer", "state id as stored, -1 when absent"),
                new Field("machines[].states[].name", "string", "state name"),
                new Field("machines[].states[].generator", "object|null", "root of the state's generator tree, null when the reference did not resolve"),
                new Field("machines[].states[].generator.kind", "string", "clip, blend, select, machine, referencePose, externalBehaviour, sequence, shared (cycle or budget stub) or unmapped (placeholder)"),
                new Field("machines[].states[].generator.class", "string", "Havok class of the generator object"),
                new Field("machines[].states[].generator.name", "string", "generator name as stored"),
                new Field("machines[].states[].generator.animationName", "string", "clip animation reference, empty unless kind is clip"),
                new Field("machines[].states[].generator.detail", "string", "external behaviour or sequence named, why a node is shared, repeated as a machine reference, or unmapped"),
                new Field("machines[].states[].generator.speed", "number", "clip playback speed as stored, 1 when the field is absent"),
                new Field("machines[].states[].generator.weight", "number", "blend child weight as stored, 1 when absent; carried, not applied by this tool"),
                new Field("machines[].states[].generator.machine", "integer|null", "index into machines when kind is machine, null otherwise; follow machines[index] for its states"),
                new Field("machines[].states[].generator.children", "array", "child nodes of the same shape; empty for leaves, shared stubs and machine references"),
                new Field("machines[].transitions", "array", "transitions owned by the machine, wildcards included"),
                new Field("machines[].transitions[].from", "integer", "source state id, -1 for a wildcard transition"),
                new Field("machines[].transitions[].to", "integer", "target state id, -1 when unreadable"),
                new Field("machines[].transitions[].wildcard", "boolean", "true when the row came from wildcardTransitions"),
                new Field("machines[].transitions[].eventId", "integer", "event id, -1 when the transition needs no event"),
                new Field("machines[].transitions[].eventName", "string", "event name from hkbBehaviorGraphStringData, empty when out of range"),
                new Field("machines[].transitions[].conditionClass", "string", "class of the condition object"),
                new Field("machines[].transitions[].conditionText", "string", "expression, condition string or class name of the condition"),
                new Field("machines[].transitions[].duration", "number", "transition effect duration in seconds, 0 when absent"),
                new Field("events", "string[]", "hkbBehaviorGraphStringData event names"),
                new Field("variables", "string[]", "hkbBehaviorGraphStringData variable names"),
                new Field("gaps", "array", "what the plan could not carry, with the count of each kind"),
                new Field("gaps[].key", "string", "gap identifier: blend-branch-not-wired, blend-weight-ignored, generator-unmapped, generator-reference-unresolved or plan-node-budget"),
                new Field("gaps[].count", "integer", "how many times this gap was met"),
                new Field("gaps[].explanation", "string", "what the gap means for a consumer"),
                new Field("unmappedGenerators", "array", "generator classes that fell back to a placeholder, sorted by class"),
                new Field("unmappedGenerators[].class", "string", "generator class name"),
                new Field("unmappedGenerators[].count", "integer", "how many times it was met"),
                new Field("warnings", "string[]", "readable form of the gaps, capped at GraphPlan.MaximumWarnings"),
                new Field("stats", "object", "counts over the whole plan"),
                new Field("stats.states", "integer", "states over every machine"),
                new Field("stats.transitions", "integer", "transitions over every machine"),
                new Field("stats.statesWithGenerator", "integer", "states whose generator is not a placeholder"),
                new Field("stats.transitionsWithEvent", "integer", "transitions naming an event"),
                new Field("stats.transitionsWithCondition", "integer", "transitions carrying condition text"),
                new Field("stats.blendNodes", "integer", "blend and selector nodes resolved"),
                new Field("stats.missingGenerators", "integer", "state generator references that did not resolve"),
                new Field("stats.revisitedGenerators", "integer", "generators met again while still on the resolution path, carried as shared cycle stubs"),
                new Field("notes", "string[]", "conditions the plan met while building, empty when none"),
            }),
            new Format(ExportFormats.Collision, "Bodies, materials and shapes decoded from one bhk collision system by NifCollisionDecoder, with the collision object that names it.", new[]
            {
                new Field("format", "string", "always " + ExportFormats.Collision),
                new Field("version", "integer", "format version, currently 1"),
                new Field("source", "string", "input path as given, relative to the batch root"),
                new Field("block", "integer", "NIF block index carrying the system"),
                new Field("type", "string", "NIF block type, bhkPhysicsSystem or bhkRagdollSystem"),
                new Field("sha256", "string", "hash of the decoder's flat record, neither of the NIF nor of the attribution"),
                new Field("pointerSize", "integer", "pointer width the blob declares"),
                new Field("lengthPrefixed", "boolean", "true when the payload carried a four-byte length word"),
                new Field("systemClass", "string", "hknpPhysicsSystemData or hknpRagdollData"),
                new Field("attribution", "object|null", "the bhkNPCollisionObject that names this system; null when no object names it, so the system belongs to no body or node"),
                new Field("attribution.ownerBlock", "integer", "NIF block index of the collision object"),
                new Field("attribution.flags", "integer", "bhkCOFlags word as stored; a mask this tool does not interpret"),
                new Field("attribution.systemRef", "integer", "the object's Data reference, equal to block whenever attribution is present"),
                new Field("attribution.bodyId", "integer", "Body ID as stored on the object"),
                new Field("attribution.targetBlock", "integer", "the object's Target block, -1 when it names none"),
                new Field("attribution.targetNode", "integer", "index of the target in the NIF's node list, -1 when the target is not a node type"),
                new Field("attribution.targetNodeName", "string", "name of the target node, empty when there is none"),
                new Field("bodies", "array", "bodies in bodyCinfos order"),
                new Field("bodies[].index", "integer", "body index"),
                new Field("bodies[].shape", "integer", "index into shapes, -1 when the shape reference did not resolve"),
                new Field("bodies[].materialId", "integer", "material index as stored"),
                new Field("bodies[].materialHash", "integer", "hknpBSMaterial uiMaterialCRC for the material, 0 when absent"),
                new Field("bodies[].flags", "integer", "body flags as stored"),
                new Field("bodies[].position", "number[4]", "x, y, z, w as stored"),
                new Field("bodies[].orientation", "number[4]", "quaternion as x, y, z, w"),
                new Field("bodies[].response", "object", "material response; default when the material index is absent or out of range"),
                new Field("bodies[].response.dynamicFriction", "number", "half-precision value widened"),
                new Field("bodies[].response.staticFriction", "number", "half-precision value widened"),
                new Field("bodies[].response.restitution", "number", "half-precision value widened"),
                new Field("bodies[].response.frictionCombine", "integer", "combine policy as stored"),
                new Field("bodies[].response.restitutionCombine", "integer", "combine policy as stored"),
                new Field("shapes", "array", "shapes in depth-first body order"),
                new Field("shapes[].index", "integer", "shape index"),
                new Field("shapes[].class", "string", "Havok class name of the shape"),
                new Field("shapes[].kind", "string", "sphere, convexPolytope, compound, compressedMesh or unknown"),
                new Field("shapes[].convexRadius", "number", "hknpShape convex radius in Havok units"),
                new Field("shapes[].vertexCount", "integer", "vertices / 3"),
                new Field("shapes[].vertices", "number[]", "flat x, y, z triples in Havok units, unconverted"),
                new Field("shapes[].triangles", "integer[]", "flat index triples into vertices"),
                new Field("shapes[].instances", "array", "compound instances, in element order with holes removed"),
                new Field("shapes[].instances[].shape", "integer", "index into shapes"),
                new Field("shapes[].instances[].transform", "number[16]", "16 floats as decoded, Havok matrix order"),
                new Field("shapes[].instances[].scale", "number[4]", "x, y, z, w scale as stored"),
                new Field("notes", "string[]", "what the decoder refused or capped, empty when none"),
            }),
            new Format(ExportFormats.Index, "index.json: what this run wrote, in one deterministic list.", new[]
            {
                new Field("format", "string", "always " + ExportFormats.Index),
                new Field("version", "integer", "format version, currently 1"),
                new Field("documents", "array", "one entry per written document, sorted by target"),
                new Field("documents[].kind", "string", "skeletons, animations, graphs or collision"),
                new Field("documents[].source", "string", "input path as given, relative to the batch root"),
                new Field("documents[].target", "string", "output path relative to the output directory"),
                new Field("documents[].inputSha256", "string", "SHA-256 of the input file"),
                new Field("documents[].sha256", "string", "SHA-256 of the written document"),
                new Field("documents[].bytes", "integer", "document length in bytes"),
            }),
            new Format("bgs.export-errors", "errors.jsonl: one JSON object per line, one line per failed input.", new[]
            {
                new Field("source", "string", "input path as given, relative to the batch root"),
                new Field("message", "string", "what failed, as the reader reported it"),
            }),
        };

        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.Schema)
            .Name("version").Number(ExportFormats.Version)
            .Name("notes").Text("Every document the export tool writes is listed here with its fields. Units are Havok units wherever the file stores them; nothing is scaled or converted.")
            .Name("formats").Array();

        foreach (var format in formats)
        {
            json.Object()
                .Name("name").Text(format.Name)
                .Name("version").Number(ExportFormats.Version)
                .Name("summary").Text(format.Summary)
                .Name("fields").Array();

            foreach (var field in format.Fields)
            {
                json.Object()
                    .Name("path").Text(field.Path)
                    .Name("type").Text(field.Type)
                    .Name("notes").Text(field.Notes)
                    .EndObject();
            }

            json.EndArray().EndObject();
        }

        json.EndArray();
        return json.EndObject().Finish();
    }
}
