import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import type { NifEffectParams } from './NifViewport';
import {
  EFFECT_DEFAULT_ALPHA, effectMaterialPlan, isMeshPickable, makeCellEffectMaterial, setCellEffectsVisible,
} from './cellEffectMaterial';

function effect(overrides: Partial<NifEffectParams> = {}): NifEffectParams {
  return {
    baseColor: [1, 1, 1, 1],
    baseColorScale: 1,
    alphaBlend: false,
    alphaDstBlend: 0,
    useFalloff: false,
    falloffStartAngle: 0,
    falloffStopAngle: 1,
    falloffStartOpacity: 1,
    falloffStopOpacity: 1,
    emissiveColor: [0, 0, 0],
    emissiveMultiple: 1,
    softFalloffDepth: 0,
    vertexAlpha: false,
    twoSided: false,
    ...overrides,
  } as NifEffectParams;
}

const SHADER_VERTEX = '#include <common>\nvoid main() {\n#include <project_vertex>\n}';
const SHADER_FRAGMENT = '#include <common>\nvoid main() {\n#include <color_fragment>\n#include <opaque_fragment>\n}';

function compile(mat: THREE.MeshBasicMaterial) {
  const shader = {
    uniforms: {} as Record<string, { value: unknown }>,
    vertexShader: SHADER_VERTEX,
    fragmentShader: SHADER_FRAGMENT,
  };
  mat.onBeforeCompile(shader as unknown as THREE.WebGLProgramParametersWithUniforms, null as unknown as THREE.WebGLRenderer);
  return shader;
}

describe('effectMaterialPlan', () => {
  it('scales the base colour and clamps each channel to one', () => {
    const plan = effectMaterialPlan(effect({ baseColor: [0.5, 0.2, 0.9, 1], baseColorScale: 3 }));
    expect(plan.colour[0]).toBe(1);
    expect(plan.colour[1]).toBeCloseTo(0.6, 9);
    expect(plan.colour[2]).toBe(1);
  });

  it('takes the opacity from the base colour alpha and leaves falloff to the shader', () => {
    const plan = effectMaterialPlan(effect({
      baseColor: [1, 1, 1, 0.4], useFalloff: true, falloffStartOpacity: 0, falloffStopOpacity: 1,
    }));
    expect(plan.opacity).toBeCloseTo(0.4, 9);
    expect(plan.falloff).toEqual({ startAngle: 0, stopAngle: 1, startOpacity: 0, stopOpacity: 1 });
  });

  it('uses additive blending when the destination blend is zero, and normal blending otherwise', () => {
    expect(effectMaterialPlan(effect({ alphaDstBlend: 0 })).additive).toBe(true);
    expect(effectMaterialPlan(effect({ alphaDstBlend: 1 })).additive).toBe(false);
    expect(effectMaterialPlan(effect({ alphaDstBlend: undefined })).additive).toBe(true);
  });

  it('is two-sided only when the effect says so', () => {
    expect(effectMaterialPlan(effect({ twoSided: true })).twoSided).toBe(true);
    expect(effectMaterialPlan(effect({ twoSided: false })).twoSided).toBe(false);
  });

  it('falls back to a soft low-alpha additive glow without an effect block', () => {
    expect(effectMaterialPlan(undefined)).toEqual({
      colour: [1, 1, 1], opacity: EFFECT_DEFAULT_ALPHA, additive: true, twoSided: true, vertexAlpha: false, falloff: null,
    });
    expect(EFFECT_DEFAULT_ALPHA).toBeLessThanOrEqual(0.2);
  });
});

describe('makeCellEffectMaterial', () => {
  it('builds a transparent, depth-neutral, unlit material that is flagged as an effect', () => {
    const mat = makeCellEffectMaterial(effect());
    expect(mat.transparent).toBe(true);
    expect(mat.depthWrite).toBe(false);
    expect(mat.toneMapped).toBe(false);
    expect(mat.userData.cellEffect).toBe(true);
    expect(mat.opacity).toBeLessThanOrEqual(1);
  });

  it('uses the blending, side and opacity of the effect block', () => {
    const mat = makeCellEffectMaterial(effect({ baseColor: [1, 0, 0, 0.6], alphaDstBlend: 1, twoSided: true }));
    expect(mat.blending).toBe(THREE.NormalBlending);
    expect(mat.side).toBe(THREE.DoubleSide);
    expect(mat.opacity).toBeCloseTo(0.6, 9);
    expect(mat.color.r).toBe(1);
  });

  it('defaults to a double-sided additive glow when the effect is missing', () => {
    const mat = makeCellEffectMaterial(undefined);
    expect(mat.blending).toBe(THREE.AdditiveBlending);
    expect(mat.side).toBe(THREE.DoubleSide);
    expect(mat.opacity).toBe(EFFECT_DEFAULT_ALPHA);
  });

  it('keeps the flat-colour shader path when there is no falloff, with its own cache key', () => {
    const mat = makeCellEffectMaterial(effect());
    const shader = compile(mat);
    expect(shader.uniforms.fxVertexAlpha.value).toBe(0);
    expect(shader.uniforms.fxFalloff.value).toEqual(new THREE.Vector4(0, 1, 1, 1));
    expect(shader.fragmentShader).not.toContain('fxNdotV');
    expect(mat.customProgramCacheKey()).toBe('cell-effect');
  });

  it('patches a view-angle falloff into the alpha when falloff is enabled', () => {
    const mat = makeCellEffectMaterial(effect({
      useFalloff: true, falloffStartAngle: 0.2, falloffStopAngle: 0.8, falloffStartOpacity: 0.1, falloffStopOpacity: 0.9,
    }));
    const shader = compile(mat);
    expect(shader.uniforms.fxFalloff.value).toEqual(new THREE.Vector4(0.2, 0.8, 0.1, 0.9));
    expect(shader.vertexShader).toContain('fxViewNormal = normalMatrix * normal;');
    expect(shader.fragmentShader).toContain('diffuseColor.a *= mix(fxFalloff.z, fxFalloff.w, fxT);');
    expect(mat.customProgramCacheKey()).toBe('cell-effect-falloff');
  });

  it('applies vertex alpha only when the effect asks for it', () => {
    expect(compile(makeCellEffectMaterial(effect({ vertexAlpha: true }))).uniforms.fxVertexAlpha.value).toBe(1);
    const shader = compile(makeCellEffectMaterial(effect({ vertexAlpha: false })));
    expect(shader.fragmentShader).toContain('diffuseColor.rgb *= vColor.rgb;');
    expect(shader.fragmentShader).toContain('mix(1.0, vColor.a, fxVertexAlpha)');
  });
});

describe('setCellEffectsVisible', () => {
  it('hides and shows only effect materials and leaves the others alone', () => {
    const scene = new THREE.Scene();
    const fx = new THREE.Mesh(new THREE.BufferGeometry(), makeCellEffectMaterial(effect()));
    const plain = new THREE.Mesh(new THREE.BufferGeometry(), new THREE.MeshBasicMaterial());
    scene.add(fx, plain);
    setCellEffectsVisible(scene, false);
    expect((fx.material as THREE.Material).visible).toBe(false);
    expect(plain.material.visible).toBe(true);
    expect(isMeshPickable(fx)).toBe(false);
    expect(isMeshPickable(plain)).toBe(true);
    setCellEffectsVisible(scene, true);
    expect((fx.material as THREE.Material).visible).toBe(true);
    expect(isMeshPickable(fx)).toBe(true);
  });

  it('does not pick a mesh that is hidden by its own visibility flag', () => {
    const mesh = new THREE.Mesh(new THREE.BufferGeometry(), new THREE.MeshBasicMaterial());
    mesh.visible = false;
    expect(isMeshPickable(mesh)).toBe(false);
  });
});
