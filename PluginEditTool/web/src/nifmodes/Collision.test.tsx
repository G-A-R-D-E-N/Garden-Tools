import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from '../backend';
import Collision from './Collision';

const SOURCE = 'C:\\mods\\body.nif';
const OUT = 'C:\\mods\\body_collision.nif';
const DONOR = 'C:\\mods\\donor.nif';

const donorBodyOf = (index: number, motion: 'static' | 'dynamic') => ({
  index, motion, mass: 0, layer: 1, layerName: '', materialCrcs: [],
  shape: { kind: 'convex', class: 'hknpConvexShape', vertices: 8, triangles: 12 },
});
const donorInfo = {
  systems: [
    { block: 5, owned: true, bodyId: 0, node: 'Scene Root', constraints: [], unsupported: [], bodies: [donorBodyOf(0, 'static')] },
    { block: 7, owned: false, bodyId: 0, node: 'Hand', constraints: [], unsupported: [], bodies: [donorBodyOf(0, 'static')] },
    { block: 9, owned: true, bodyId: 1, node: 'Tail', constraints: [], unsupported: [], bodies: [donorBodyOf(0, 'static'), donorBodyOf(1, 'dynamic')] },
  ],
  findings: [],
};

const info = {
  systems: [{
    block: 5, bodyId: 0, node: 'Scene Root', constraints: [],
    unsupported: [{ class: 'hknpSphereOnlyShape', object: 9 }],
    bodies: [{
      index: 0, motion: 'static', mass: 0, layer: 0, materialCrcs: ['0x064003D4'],
      shape: { kind: 'convex', class: 'hknpConvexShape', vertices: 8, triangles: 12 },
    }],
  }],
  findings: [{ issue: 'layer_zero', block: 5 }],
};

function hostWith(extra: Partial<NifHost> = {}): NifHost {
  return {
    CollisionInfo: vi.fn(async () => JSON.stringify(info)),
    Tree: vi.fn(async () => JSON.stringify({
      blocks: [{ name: 'Scene Root', group: 'Nodes' }, { name: 'Body', group: 'Shapes' }],
    })),
    CollisionSet: vi.fn(async () => 'RESULT: collision set OK (in-process)'),
    CollisionRemove: vi.fn(async () => 'RESULT: collision removed (in-process)'),
    BrowseForFile: vi.fn(async () => ''),
    BrowseForSave: vi.fn(async () => ''),
    ...extra,
  } as unknown as NifHost;
}

async function loadWith(host: NifHost) {
  render(<Collision nif={host} appendLog={() => {}} />);
  fireEvent.change(screen.getByLabelText('NIF path'), { target: { value: SOURCE } });
  fireEvent.click(screen.getByRole('button', { name: /Load collision/ }));
  await screen.findByText(/Static body on layer 0/);
}

function saveAs(path: string) {
  fireEvent.change(screen.getByLabelText('Save As NIF'), { target: { value: path } });
}

afterEach(() => cleanup());

describe('Collision mode', () => {
  it('shows the bodies, their layer and material CRCs, lint findings and the authored-collision warning', async () => {
    // Regression: the mode showed no bodies or lint findings, so a static body on layer 0 looked clean.
    await loadWith(hostWith());
    expect(screen.getByText('static')).toBeTruthy();
    expect(screen.getByText('0x064003D4')).toBeTruthy();
    expect(screen.getByText('convex')).toBeTruthy();
    expect(screen.getByText('Unsupported shape hknpSphereOnlyShape (object 9)')).toBeTruthy();
    expect(screen.getByText(/has not been tested in the game until the owner confirms it/)).toBeTruthy();
    expect(screen.getByText(/Mesh \(static only\)/, { selector: 'option' })).toBeTruthy();
  });

  it('shows a native refusal verbatim in the output', async () => {
    // Regression: a native refusal was replaced by a generic failure, hiding why the collision was not written.
    const refusal = "Error: node 'Body' has no shape geometry to build collision from";
    await loadWith(hostWith({ CollisionSet: vi.fn(async () => refusal) }));
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    expect(await screen.findByText(refusal)).toBeTruthy();
  });

  it('refuses a Save As path equal to the source before it reaches the native writer', async () => {
    // Regression: a Save As path equal to the source NIF was sent to the native writer, which could overwrite the source.
    const set = vi.fn(async () => '');
    await loadWith(hostWith({ CollisionSet: set }));
    saveAs(SOURCE);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    expect(await screen.findByText(/Refused: the Save As path is the source NIF/)).toBeTruthy();
    expect(set).not.toHaveBeenCalled();
  });

  it('sends a generated hull with its mass to the Save As path', async () => {
    // Regression: the chosen shape or the hull mass was dropped before the set call, so the written body came out static.
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    await loadWith(hostWith({ CollisionSet: set }));
    fireEvent.change(screen.getByLabelText('Shape'), { target: { value: 'hull' } });
    fireEvent.change(screen.getByLabelText('Mass in kg'), { target: { value: '12' } });
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'kind', '', 'hull', 12));
  });

  it('shows the mass field only for a hull', async () => {
    // Regression: a mass field showed for box or mesh, where the typed mass is ignored or refused.
    await loadWith(hostWith());
    expect(screen.queryByLabelText('Mass in kg')).toBeNull();
    fireEvent.change(screen.getByLabelText('Shape'), { target: { value: 'mesh' } });
    expect(screen.queryByLabelText('Mass in kg')).toBeNull();
  });

  it('removes the collision of the chosen node into the Save As path', async () => {
    // Regression: remove wrote to the source NIF or to another node, leaving the collision the user meant to drop in place.
    const remove = vi.fn(async () => 'RESULT: collision removed (in-process)');
    await loadWith(hostWith({ CollisionRemove: remove }));
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Remove collision/ }));
    await waitFor(() => expect(remove).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT));
  });

  // Detects: the layer column showing only the number, so a body on a named COLL layer looks the same as one on an unknown layer.
  it('shows the COLL layer name of a body and the layer number when no name is known', async () => {
    const named = {
      ...info,
      systems: [{ ...info.systems[0], bodies: [
        { ...info.systems[0].bodies[0], index: 0, layer: 1, layerName: 'L_STATIC' },
        { ...info.systems[0].bodies[0], index: 1, layer: 5, layerName: '' },
      ] }],
    };
    await loadWith(hostWith({ CollisionInfo: vi.fn(async () => JSON.stringify(named)) }));
    expect(screen.getByText('L_STATIC')).toBeTruthy();
    expect(screen.getByText('5')).toBeTruthy();
  });

  // Detects: a donor copy that always takes the first body, or leaving the donor field resetting a chosen body to the first.
  it('lists the donor bodies that own a node and sends the chosen one as donor_body', async () => {
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    const host = hostWith({
      CollisionInfo: vi.fn(async (p: string) => JSON.stringify(p === DONOR ? donorInfo : info)),
      CollisionSet: set,
    });
    await loadWith(host);
    fireEvent.change(screen.getByLabelText('Source'), { target: { value: 'donor' } });
    fireEvent.change(screen.getByLabelText('Donor NIF'), { target: { value: DONOR } });
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    const picker = await screen.findByLabelText('Donor body');
    expect(screen.queryByRole('option', { name: /Hand/ })).toBeNull();
    fireEvent.change(picker, { target: { value: '1' } });
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'donor_nif', DONOR, '', 0, 1));
  });
});
