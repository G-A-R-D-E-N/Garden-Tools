import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import * as THREE from 'three';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import NifViewport, { type NifGeo } from './NifViewport';
import type { CollisionGeo } from './util/collisionGeometry';

const rendered = vi.hoisted(() => ({ scene: null as unknown }));

vi.mock('three', async importOriginal => {
  const actual = await importOriginal<typeof import('three')>();
  class FakeRenderer {
    domElement = document.createElement('canvas');
    setSize() {}
    setPixelRatio() {}
    render(scene: unknown) { rendered.scene = scene; }
    dispose() {}
  }
  class FakeTextureLoader {
    load(_url: string, onLoad: (tex: import('three').Texture) => void) { onLoad(new actual.Texture()); }
  }
  return { ...actual, WebGLRenderer: FakeRenderer, TextureLoader: FakeTextureLoader };
});

vi.mock('three/addons/controls/OrbitControls.js', () => ({
  OrbitControls: class {
    target = { copy() {} };
    enableDamping = false;
    dampingFactor = 0;
    update() {}
    dispose() {}
  },
}));

const GEO: NifGeo = {
  fo4: true,
  shapes: [{ name: 'Tri', verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [], uvs: [], block: 7 }],
};

const COLLISION: CollisionGeo = {
  bodies: [{
    node: 'Col', motion: 'static',
    transform: { t: [0, 0, 0], r: [1, 0, 0, 0, 1, 0, 0, 0, 1], s: 1 },
    shapes: [{ vertices: [0, 0, 0, 1, 0, 0, 0, 1, 0], triangles: [0, 1, 2] }],
  }],
  skipped: [],
};

const HIGHLIGHT = { block: 7, verts: [0] };

const scene = () => rendered.scene as THREE.Scene;

const hasHighlightPoints = () => scene().children.some(child => child instanceof THREE.Points);

const collisionFills = () => {
  const found: THREE.Object3D[] = [];
  scene().traverse(obj => {
    if (obj instanceof THREE.Mesh && obj.material instanceof THREE.MeshBasicMaterial) found.push(obj);
  });
  return found;
};

beforeEach(() => {
  vi.stubGlobal('requestAnimationFrame', () => 0);
  vi.stubGlobal('cancelAnimationFrame', () => {});
  vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} });
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NifViewport selection highlight', () => {
  it('keeps the selected vertex points when the texture loader changes and the scene is rebuilt (detects highlight points lost on a texture folder change)', () => {
    const { rerender } = render(
      <NifViewport data={GEO} wireframe={false} textured={false} highlight={HIGHLIGHT} loadTexture={vi.fn(async () => '')} />,
    );
    expect(hasHighlightPoints()).toBe(true);

    rerender(
      <NifViewport data={GEO} wireframe={false} textured={false} highlight={HIGHLIGHT} loadTexture={vi.fn(async () => '')} />,
    );
    expect(hasHighlightPoints()).toBe(true);
  });
});

describe('NifViewport collision overlay', () => {
  it('refetches the overlay when the geometry object changes and drops it when that refetch fails (detects a previous file collision left drawn after a same-path reload)', async () => {
    const loadCollision = vi.fn(async () => COLLISION);
    const { rerender } = render(
      <NifViewport data={GEO} wireframe={false} textured={false} loadCollision={loadCollision} />,
    );
    fireEvent.click(screen.getByRole('checkbox'));
    await waitFor(() => expect(collisionFills()).toHaveLength(1));
    expect(loadCollision).toHaveBeenCalledTimes(1);

    loadCollision.mockRejectedValueOnce(new Error('collision read failed'));
    rerender(<NifViewport data={{ ...GEO }} wireframe={false} textured={false} loadCollision={loadCollision} />);
    await waitFor(() => expect(loadCollision).toHaveBeenCalledTimes(2));
    expect(await screen.findByText('collision read failed')).toBeTruthy();
    await waitFor(() => expect(collisionFills()).toHaveLength(0));
  });
});

describe('NifViewport texture wrap', () => {
  // Constraint, detects: viewer textures always RepeatWrapping, so a clamped effect or lighting texture tiles at the UV border.
  it('applies the shape texture clamp mode to the loaded diffuse map', async () => {
    const uvs = [0, 0, 1, 0, 0, 1];
    const base = { verts: [0, 0, 0, 1, 0, 0, 0, 1, 0], tris: [0, 1, 2], normals: [], uvs };
    const geo: NifGeo = {
      fo4: true,
      shapes: [
        { ...base, name: 'Lit', textures: [{ slot: 0, path: 'a.dds' }], textureClamp: 1 },
        { ...base, name: 'Fx', textures: [{ slot: 0, path: 'b.dds' }], effectShader: true, textureClamp: 0 },
        { ...base, name: 'Plain', textures: [{ slot: 0, path: 'c.dds' }] },
      ],
    };
    render(<NifViewport data={geo} wireframe={false} textured loadTexture={vi.fn(async path => path)} />);
    const wraps = () => {
      const out: [number, number][] = [];
      scene().traverse(obj => {
        const map = obj instanceof THREE.Mesh ? (obj.material as THREE.MeshBasicMaterial).map : null;
        if (map) out.push([map.wrapS, map.wrapT]);
      });
      return out;
    };
    await waitFor(() => expect(wraps()).toHaveLength(3));
    expect(wraps()).toEqual([
      [THREE.ClampToEdgeWrapping, THREE.RepeatWrapping],
      [THREE.ClampToEdgeWrapping, THREE.ClampToEdgeWrapping],
      [THREE.RepeatWrapping, THREE.RepeatWrapping],
    ]);
  });
});
