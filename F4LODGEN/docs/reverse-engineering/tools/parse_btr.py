#!/usr/bin/env python3
"""parse_btr.py -- byte-level parser for LODGen .btr/.bto/.nif output.

Parses the NIF-family files written by LODGenerator.NifMain and verifies the
header, every block (field-by-field against the confirmed Write() layouts),
and the footer, reporting any deviation from the declared block sizes.

Spec basis: reverse-engineering/XLODGEN_OUTPUT_FORMAT.md, cross-checked against the
decompiled writer classes (NiObjectNET, NiAVObject, NiNode, BSMultiBoundNode,
BSMultiBound, BSMultiBoundAABB, BSShaderTextureSet, BSLightingShaderProperty,
NiGeometry, NiGeometryData, NiTriBasedGeomData, NiTriShapeData). The layout
facts embedded here were VERIFIED byte-for-byte against a real file generated
by the bundled LODGenx64.exe (version 3.0.36.0, run under wine-mono 4.8).

Usage:
    python3 parse_btr.py <file.btr|.bto|.nif>

Exit status: 0 = every block consumed exactly its declared size and the file
ends exactly at the footer; 1 = mismatch (with details).

Field encodings (all little-endian):
    int32/uint32/u16/u8, float32, bool = 1 byte
    sized string  = uint32 length + bytes
    header string = 1-byte len+1 + bytes + NUL
    Vector3       = 3 x float32      Color3 = 3 x float32
    UVCoord       = 2 x float32      Matrix33 = 9 x float32
"""

import struct
import sys


class Reader:
    def __init__(self, data):
        self.data = data
        self.off = 0

    def u8(self):
        v = self.data[self.off]
        self.off += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.data, self.off)[0]
        self.off += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.data, self.off)[0]
        self.off += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.data, self.off)[0]
        self.off += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.data, self.off)[0]
        self.off += 4
        return v

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def hstr(self):
        n = self.u8()
        s = self.data[self.off:self.off + n - 1].decode("latin1")
        self.off += n
        return s

    def sstr(self):
        n = self.u32()
        s = self.data[self.off:self.off + n].decode("latin1")
        self.off += n
        return s

    def remaining(self):
        return len(self.data) - self.off


def parse(path):
    data = open(path, "rb").read()
    r = Reader(data)
    problems = []

    # ---- header ------------------------------------------------------------
    hs = []
    while data[r.off] != 0x0A:
        hs.append(chr(data[r.off]))
        r.off += 1
    r.off += 1
    header_string = "".join(hs)
    version = r.u32()
    endian = r.u8()
    user_version = r.u32()
    num_blocks = r.u32()
    user_version2 = r.u32()
    creator = r.hstr()
    export1 = r.hstr()
    export2 = r.hstr()
    export3 = None
    if version == 335675399 and user_version2 == 130:  # FO4
        export3 = r.hstr()
    num_block_types = r.u16()
    block_types = [r.sstr() for _ in range(num_block_types)]
    type_indices = [r.u16() for _ in range(num_blocks)]
    block_sizes = None
    strings = []
    if version > 335544325:
        block_sizes = [r.u32() for _ in range(num_blocks)]
        num_strings = r.u32()
        r.u32()  # max string length (recomputed at write)
        strings = [r.sstr() for _ in range(num_strings)]
    unknown_int2 = r.u32()

    print(f"headerString : {header_string!r}")
    print(f"version      : {version:#x}  endian={endian}  userVersion={user_version}  "
          f"numBlocks={num_blocks}  userVersion2={user_version2}")
    print(f"creator      : {creator!r}")
    print(f"exportInfo   : {export1!r} | {export2!r}" + (f" | {export3!r}" if export3 is not None else ""))
    print(f"blockTypes   : {block_types}")
    print(f"typeIndices  : {type_indices} -> {[block_types[i] for i in type_indices]}")
    print(f"strings      : {strings!r}")
    print(f"unknownInt2  : {unknown_int2}")

    # ---- block parsers -----------------------------------------------------
    def ni_objectnet(label):
        if version <= 335544325:
            print(f"    {label}name: {r.sstr()!r}")
        else:
            print(f"    {label}nameIdx={r.i32()}")
        num_extra = r.u32()
        extra = [r.i32() for _ in range(num_extra)]
        ctrl = r.i32()
        print(f"    {label}numExtraData={num_extra} extra={extra} controller={ctrl}")

    def ni_avobject(label):
        ni_objectnet(label)
        flags = r.u16()
        if user_version >= 11 and user_version2 >= 26:
            flags2 = r.u16()
        else:
            flags2 = None
        translation = r.vec3()
        rotation = [r.f32() for _ in range(9)]
        scale = r.f32()
        if version <= 335544325 or user_version <= 11:
            num_props = r.u32()
            props = [r.i32() for _ in range(num_props)]
        else:
            num_props, props = 0, []
        collision = r.i32()
        print(f"    {label}flags={flags}" + (f" flags2={flags2}" if flags2 is not None else "") +
              f" translation={translation} scale={scale}")
        print(f"    {label}numProps={num_props} props={props} collisionObject={collision}")
        return translation, scale

    def ni_node(label):
        ni_avobject(label)
        num_children = r.u32()
        children = [r.i32() for _ in range(num_children)]
        if user_version2 < 130:
            num_effects = r.u32()
            effects = [r.i32() for _ in range(num_effects)]
        else:
            num_effects, effects = 0, []
        print(f"    {label}numChildren={num_children} children={children} "
              f"numEffects={num_effects} effects={effects}")
        return children

    def ni_geometry(label):
        ni_avobject(label)
        data_ref = r.i32()
        skin = r.i32()
        if version > 335544325:
            num_materials = r.u32()
            names = [r.sstr() for _ in range(num_materials)]
            extra_data = [r.sstr() for _ in range(num_materials)]
            active = r.u32()
            dirty = r.u8()
            bs_props = None
            if user_version == 12:
                bs_props = (r.i32(), r.i32())
            print(f"    {label}data={data_ref} skinInstance={skin} numMaterials={num_materials} "
                  f"activeMaterial={active} dirtyFlag={dirty}" +
                  (f" bsProperties={bs_props}" if bs_props is not None else ""))
        else:
            has_shader = r.u8()
            print(f"    {label}data={data_ref} skinInstance={skin} hasShader={has_shader}")
        return data_ref

    def ni_geometry_data(label):
        # NOTE: NiGeometryData : NiObject -- there is NO NiObjectNET prefix
        # (no nameIdx/numExtraData/controller). The block starts with groupID.
        group_id = r.u32()
        num_verts = r.u16()
        keep = r.u8()
        compress = r.u8()
        has_verts = r.u8()
        if has_verts:
            verts = [r.vec3() for _ in range(num_verts)]
        else:
            verts = []
        num_uv_sets = r.u8()        # 1 byte, not u32!
        extra_vector_flags = r.u8()  # 1 byte, not u32!
        skyrim_material = r.u32() if user_version == 12 else None
        has_normals = r.u8()
        normals = [r.vec3() for _ in range(num_verts)] if has_normals else []
        if has_normals and (extra_vector_flags & 0x10) == 16:
            tangents = [r.vec3() for _ in range(num_verts)]
            bitangents = [r.vec3() for _ in range(num_verts)]
        else:
            tangents = bitangents = []
        center = r.vec3()
        radius = r.f32()
        has_colors = r.u8()
        if has_colors:
            colors = [tuple(r.u8() for _ in range(4)) for _ in range(num_verts)]
        else:
            colors = []
        uvs = []
        if num_uv_sets & 1:  # single flat UV array, no per-set hasUV flags
            uvs = [(r.f32(), r.f32()) for _ in range(num_verts)]
        consistency = r.u16()
        additional = r.i32()
        num_tris = r.u16()
        num_tri_points = r.u32()  # uint32, not u16!
        has_tris = r.u8()
        tris = [tuple(r.u16() for _ in range(3)) for _ in range(num_tris)] if has_tris else []
        num_match = r.u16()
        matches = []
        for _ in range(num_match):
            n = r.u16()
            matches.append([r.u16() for _ in range(n)])
        print(f"    {label}groupID={group_id} numVertices={num_verts} keepFlags={keep} "
              f"compressFlags={compress} hasVertices={has_verts}")
        print(f"    {label}numUVSets={num_uv_sets} extraVectorFlags={extra_vector_flags}" +
              (f" skyrimMaterial={skyrim_material}" if skyrim_material is not None else "") +
              f" hasNormals={has_normals} tangents={len(tangents)} bitangents={len(bitangents)}")
        print(f"    {label}center={center} radius={radius} hasVertexColors={has_colors} "
              f"uvs={len(uvs)} consistencyFlags={consistency} additionalData={additional}")
        print(f"    {label}numTriangles={num_tris} numTrianglePoints={num_tri_points} "
              f"hasTriangles={has_tris} numMatchGroups={num_match}")
        if num_verts:
            lo = min(v[2] for v in verts)
            hi = max(v[2] for v in verts)
            print(f"    {label}z-range: {lo:.1f} .. {hi:.1f}  first vert={verts[0] if verts else None}")
        return num_verts, num_tris

    def ni_bs_lighting_shader():
        shader_type = r.u32()
        ni_objectnet("")
        flags1 = r.u32()
        flags2 = r.u32()
        uv_offset = (r.f32(), r.f32())
        uv_scale = (r.f32(), r.f32())
        tex_set = r.i32()
        emissive = (r.f32(), r.f32(), r.f32())
        emissive_mult = r.f32()
        if version == 335675399 and user_version2 == 130:
            root_material = r.i32()
        else:
            root_material = None
        clamp_mode = r.u32()
        alpha = r.f32()
        refraction = r.f32()
        glossiness = r.f32()
        specular = (r.f32(), r.f32(), r.f32())
        spec_strength = r.f32()
        if version == 335675399 and user_version2 == 130:
            extra_fo4 = (r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32())
        else:
            extra_fo4 = None
            lighting_effect = (r.f32(), r.f32())
        # shader-type-specific tail (none for terrain type 18)
        tail = []
        if shader_type == 1:
            tail.append(("envMapScale", r.f32()))
        elif shader_type == 5:
            tail.append(("skinTintColor", r.vec3()))
        elif shader_type == 6:
            tail.append(("hairTintColor", r.vec3()))
        elif shader_type == 7:
            tail.append(("maxPasses/scale", (r.u32(), r.f32())))
        print(f"    shaderType={shader_type} flags1={flags1:#x} flags2={flags2:#x} "
              f"uvOffset={uv_offset} uvScale={uv_scale} textureSet={tex_set}")
        print(f"    emissive={emissive} emissiveMultiple={emissive_mult} " +
              (f"rootMaterial={root_material} " if root_material is not None else "") +
              f"clampMode={clamp_mode} alpha={alpha} refraction={refraction} "
              f"glossiness={glossiness} specular={specular} specStrength={spec_strength}" +
              (f" lightingEffects={lighting_effect}" if extra_fo4 is None else f" fo4Extras={extra_fo4}"))
        if tail:
            print(f"    shader tail: {tail}")

    # ---- blocks ------------------------------------------------------------
    ok = True
    for i in range(num_blocks):
        t = block_types[type_indices[i]]
        size = block_sizes[i] if block_sizes else None
        start = r.off
        print(f"\n--- block {i} [{t}] size={size} @ {start:#x}")
        try:
            if t == "BSMultiBoundNode":
                ni_node("")
                mb = r.i32()
                cull = r.u32() if user_version >= 12 else None
                print(f"    multiBound={mb} cullMode={cull}")
            elif t == "NiNode":
                ni_node("")
            elif t == "BSFadeNode":
                ni_node("")
            elif t == "BSMultiBound":  # : NiObject -- no prefix
                print(f"    data={r.i32()}")
            elif t == "BSMultiBoundAABB":  # : BSMultiBoundData : NiObject -- no prefix
                print(f"    position={r.vec3()} extent={r.vec3()}")
            elif t == "BSShaderTextureSet":  # : NiObject -- no prefix
                n = r.u32()
                print(f"    numTextures={n}")
                for k in range(n):
                    print(f"      texture[{k}] = {r.sstr()!r}")
            elif t == "BSLightingShaderProperty":
                ni_bs_lighting_shader()
            elif t in ("NiTriShape", "BSTriShape", "BSSegmentedTriShape",
                       "BSSubIndexTriShape", "BSMeshLODTriShape"):
                if t == "BSSegmentedTriShape":
                    seg_start = r.off
                ni_geometry("")
                if t == "BSSegmentedTriShape":
                    n = r.u32()
                    segs = [(r.u32(), r.u32()) for _ in range(n)]
                    print(f"    numSegments={n} segments={segs}")
            elif t in ("NiTriShapeData", "NiTriStripsData"):
                ni_geometry_data("")
            else:
                print(f"    (unparsed block type, first 16 bytes: "
                      f"{data[r.off:r.off+16].hex()})")
        except Exception as e:
            problems.append(f"block {i} [{t}]: parse error: {e}")
            ok = False
        consumed = r.off - start
        if size is not None and consumed != size:
            problems.append(f"block {i} [{t}]: consumed {consumed} bytes, "
                            f"declared size {size} (delta {consumed - size})")
            ok = False
        if size is not None:
            print(f"    -> consumed {consumed}/{size} bytes "
                  f"({'OK' if consumed == size else 'MISMATCH'})")
            r.off = start + size  # trust the declared size for further blocks

    # ---- footer ------------------------------------------------------------
    print(f"\n--- footer @ {r.off:#x}")
    roots = r.u32()
    root_idx = r.i32()
    print(f"numRoots={roots} rootIndex={root_idx}")
    if not (roots == 1 and root_idx == 0):
        problems.append(f"unexpected footer {roots},{root_idx} (expected 1, 0)")
        ok = False
    rem = r.remaining()
    print(f"file size {len(data)}, consumed {r.off}, remainder {rem}")
    if rem != 0:
        problems.append(f"{rem} trailing bytes after footer")
        ok = False

    print("\n" + "=" * 60)
    if problems:
        print(f"RESULT: MISMATCH ({len(problems)} problem(s))")
        for p in problems:
            print(f"  - {p}")
        return 1
    print("RESULT: OK -- header, all blocks, and footer match the spec exactly")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(parse(sys.argv[1]))
