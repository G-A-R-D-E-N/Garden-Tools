import * as THREE from 'three';
import type { NifEffectParams } from './NifViewport';

export const EFFECT_DEFAULT_ALPHA = 0.15;
export const EFFECT_FLAG = 'cellEffect';
const ALPHA_DST_ZERO = 0;

export interface EffectFalloff {
  startAngle: number;
  stopAngle: number;
  startOpacity: number;
  stopOpacity: number;
}

export interface EffectMaterialPlan {
  colour: [number, number, number];
  opacity: number;
  additive: boolean;
  twoSided: boolean;
  vertexAlpha: boolean;
  falloff: EffectFalloff | null;
}

const clamp01 = (value: number) => Math.min(1, Math.max(0, value));

export function effectMaterialPlan(effect: NifEffectParams | undefined): EffectMaterialPlan {
  if (!effect) {
    return {
      colour: [1, 1, 1], opacity: EFFECT_DEFAULT_ALPHA, additive: true, twoSided: true, vertexAlpha: false, falloff: null,
    };
  }
  const [r, g, b, a] = effect.baseColor;
  const scale = Number.isFinite(effect.baseColorScale) ? effect.baseColorScale : 1;
  return {
    colour: [clamp01(r * scale), clamp01(g * scale), clamp01(b * scale)],
    opacity: clamp01(a),
    additive: (effect.alphaDstBlend ?? ALPHA_DST_ZERO) === ALPHA_DST_ZERO,
    twoSided: effect.twoSided === true,
    vertexAlpha: effect.vertexAlpha === true,
    falloff: effect.useFalloff
      ? {
        startAngle: effect.falloffStartAngle,
        stopAngle: effect.falloffStopAngle,
        startOpacity: effect.falloffStartOpacity,
        stopOpacity: effect.falloffStopOpacity,
      }
      : null,
  };
}

const COLOUR_BLOCK = [
  '#ifdef USE_COLOR',
  '  diffuseColor.rgb *= vColor.rgb;',
  '  #ifdef USE_COLOR_ALPHA',
  '    diffuseColor.a *= mix(1.0, vColor.a, fxVertexAlpha);',
  '  #endif',
  '#endif',
].join('\n');

function falloffBlock(): string {
  return [
    'float fxNdotV = clamp(abs(dot(normalize(fxViewNormal), normalize(-fxViewPos))), 0.0, 1.0);',
    'float fxDenom = fxFalloff.y - fxFalloff.x;',
    'float fxT = clamp((fxNdotV - fxFalloff.x) / (abs(fxDenom) < 0.0001 ? 0.0001 : fxDenom), 0.0, 1.0);',
    'diffuseColor.a *= mix(fxFalloff.z, fxFalloff.w, fxT);',
  ].join('\n');
}

function patchEffectShader(shader: THREE.WebGLProgramParametersWithUniforms, plan: EffectMaterialPlan): void {
  const falloff = plan.falloff;
  shader.uniforms.fxVertexAlpha = { value: plan.vertexAlpha ? 1 : 0 };
  shader.uniforms.fxFalloff = {
    value: falloff
      ? new THREE.Vector4(falloff.startAngle, falloff.stopAngle, falloff.startOpacity, falloff.stopOpacity)
      : new THREE.Vector4(0, 1, 1, 1),
  };
  const varyings = 'varying vec3 fxViewNormal;\nvarying vec3 fxViewPos;';
  shader.vertexShader = shader.vertexShader
    .replace('#include <common>', `#include <common>\n${varyings}`)
    .replace('#include <project_vertex>', '#include <project_vertex>\nfxViewPos = mvPosition.xyz;\nfxViewNormal = normalMatrix * normal;');
  shader.fragmentShader = shader.fragmentShader
    .replace('#include <common>', `#include <common>\n${varyings}\nuniform float fxVertexAlpha;\nuniform vec4 fxFalloff;`)
    .replace('#include <color_fragment>', COLOUR_BLOCK)
    .replace('#include <opaque_fragment>', `${falloff ? falloffBlock() : ''}\n#include <opaque_fragment>`);
}

export function makeCellEffectMaterial(effect: NifEffectParams | undefined): THREE.MeshBasicMaterial {
  const plan = effectMaterialPlan(effect);
  const mat = new THREE.MeshBasicMaterial({
    color: new THREE.Color().setRGB(...plan.colour),
    opacity: plan.opacity,
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    side: plan.twoSided ? THREE.DoubleSide : THREE.FrontSide,
    blending: plan.additive ? THREE.AdditiveBlending : THREE.NormalBlending,
  });
  mat.userData[EFFECT_FLAG] = true;
  mat.customProgramCacheKey = () => (plan.falloff ? 'cell-effect-falloff' : 'cell-effect');
  mat.onBeforeCompile = shader => patchEffectShader(shader, plan);
  return mat;
}

export function setCellEffectsVisible(root: THREE.Object3D, visible: boolean): void {
  root.traverse(obj => {
    if (!(obj instanceof THREE.Mesh)) return;
    const mat = obj.material;
    if (!Array.isArray(mat) && mat.userData[EFFECT_FLAG] === true) mat.visible = visible;
  });
}

export function isMeshPickable(mesh: THREE.Mesh): boolean {
  return mesh.visible && (Array.isArray(mesh.material) || mesh.material.visible);
}
