
export interface ShapeMaterialData {
  diffuse: string;
  normal: string;
  smoothSpec: string;
  glow: string;
  rootMaterialPath?: string;
  alphaBlend: boolean;
  alphaTest: boolean;
  alphaTestRef: number;
  alpha: number;
  twoSided: boolean;
  decal?: boolean;
  emitEnabled: boolean;
  emittanceColor: [number, number, number];
  emittanceMult: number;
  smoothness: number;
  pbr?: boolean;
  tileU?: boolean;
  tileV?: boolean;
  format?: string;
}

export interface ShapeTextureRef { slot: number; path: string }

export interface ShapeLike {
  textureClamp?: number;
  textures?: ShapeTextureRef[];
  bgsmPath?: string;
  material?: ShapeMaterialData;
}

export interface ShapeMapPaths {
  diffuse?: string;
  normal?: string;
  smoothSpec?: string;
  glow?: string;
}

export interface ShapeMaterialPlan {
  side: 'front' | 'double';
  alphaTest?: number;
  transparent: boolean;
  opacity: number;
  roughness: number;
  metalness: number;
  emissive?: [number, number, number];
  emissiveIntensity: number;
  roughnessFromMap: boolean;
}

export const DEFAULT_ROUGHNESS = 0.8;
export const DEFAULT_METALNESS = 0.1;
export const GLOW_WITHOUT_EMIT_INTENSITY = 0.15;
export const EMISSIVE_INTENSITY_MAX = 2;

const clamp01 = (value: number) => Math.min(1, Math.max(0, value));

function nonEmpty(path: string | undefined): string | undefined {
  return path && path.trim() !== '' ? path : undefined;
}

export function shapeMapPaths(shape: ShapeLike): ShapeMapPaths {
  const material = shape.material;
  if (material) {
    return {
      diffuse: nonEmpty(material.diffuse),
      normal: nonEmpty(material.normal),
      smoothSpec: nonEmpty(material.smoothSpec),
      glow: nonEmpty(material.glow),
    };
  }
  const slot = (index: number) => nonEmpty(shape.textures?.find(t => t.slot === index)?.path);
  return {
    diffuse: slot(0),
    normal: slot(1),
  };
}

export function shapeMaterialPlan(shape: ShapeLike, hasSmoothSpecMap: boolean): ShapeMaterialPlan {
  const material = shape.material;
  const side: 'front' | 'double' = material ? (material.twoSided ? 'double' : 'front') : 'double';

  const alphaTest = material?.alphaTest
    ? Math.min(0.99, Math.max(0.01, material.alphaTestRef / 255))
    : undefined;
  const opacity = material && material.alpha > 0 ? clamp01(material.alpha) : 1;
  const transparent = Boolean(material?.alphaBlend) && alphaTest === undefined;

  const smoothness = material?.smoothness ?? 0;
  const scalarRoughness = material && smoothness > 0 ? clamp01(1 - smoothness) : DEFAULT_ROUGHNESS;
  const roughnessFromMap = hasSmoothSpecMap;

  let emissive: [number, number, number] | undefined;
  let emissiveIntensity = 1;
  if (material?.emitEnabled) {
    emissive = [...material.emittanceColor] as [number, number, number];
    emissiveIntensity = material.emittanceMult > 0
      ? Math.min(EMISSIVE_INTENSITY_MAX, material.emittanceMult)
      : 1;
  } else if (material && nonEmpty(material.glow)) {
    emissive = [1, 1, 1];
    emissiveIntensity = GLOW_WITHOUT_EMIT_INTENSITY;
  }

  return {
    side,
    alphaTest,
    transparent,
    opacity,
    roughness: roughnessFromMap ? 1 : scalarRoughness,
    metalness: DEFAULT_METALNESS,
    emissive,
    emissiveIntensity,
    roughnessFromMap,
  };
}

export interface TextureTiling { tileU: boolean; tileV: boolean }

export const TILE_BOTH: TextureTiling = { tileU: true, tileV: true };

// Invariant: textureClamp is nif.xml TexClampMode (bit 1 wraps S/U, bit 0 wraps T/V); a material's own tile flags win when present.
export function shapeTextureTiling(shape: ShapeLike): TextureTiling {
  const material = shape.material;
  if (material && (material.tileU !== undefined || material.tileV !== undefined)) {
    return { tileU: material.tileU !== false, tileV: material.tileV !== false };
  }
  const mode = shape.textureClamp;
  if (mode === undefined || !Number.isInteger(mode) || mode < 0 || mode > 3) return TILE_BOTH;
  return { tileU: (mode & 2) !== 0, tileV: (mode & 1) !== 0 };
}
