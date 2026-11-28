import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from '../backend';
import { DialogProvider } from '../dialogs';
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
  source: 'sha-donor',
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
  source: 'sha-source',
};

function hostWith(extra: Partial<NifHost> = {}): NifHost {
  return {
    CollisionInfo: vi.fn(async () => JSON.stringify(info)),
    Tree: vi.fn(async () => JSON.stringify({
      source: 'sha-source',
      blocks: [{ name: 'Scene Root', group: 'Nodes' }, { name: 'Body', group: 'Shapes' }],
    })),
    CollisionSet: vi.fn(async () => 'RESULT: collision set OK (in-process)'),
    CollisionRemove: vi.fn(async () => 'RESULT: collision removed (in-process)'),
    BrowseForFile: vi.fn(async () => ''),
    BrowseForSave: vi.fn(async () => ''),
    ...extra,
  } as unknown as NifHost;
}

async function loadWith(host: NifHost, appendLog: (line: string) => void = () => {}) {
  render(<Collision nif={host} appendLog={appendLog} />, { wrapper: DialogProvider });
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
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'kind', '', 'hull', 12, undefined, false, 'sha-source', ''));
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
    await waitFor(() => expect(remove).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, false, 'sha-source'));
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
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'donor_nif', DONOR, '', 0, 1, false, 'sha-source', 'sha-donor'));
  });

  it('lists a node name that two nodes share as disabled and keeps the unique node selected', async () => {
    const tree = { source: 'sha-source', blocks: [
      { name: 'CollisionNode', group: 'Nodes' }, { name: 'CollisionNode', group: 'Nodes' }, { name: 'Scene Root', group: 'Nodes' },
    ] };
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    await loadWith(hostWith({ Tree: vi.fn(async () => JSON.stringify(tree)), CollisionSet: set }));
    const shared = screen.getByRole('option', { name: /CollisionNode/ }) as HTMLOptionElement;
    expect(shared.disabled).toBe(true);
    expect((screen.getByLabelText('Node') as HTMLSelectElement).value).toBe('Scene Root');
    expect(set).not.toHaveBeenCalled();
  });

  it('offers no node to write when every node name is shared', async () => {
    const tree = { source: 'sha-source', blocks: [{ name: 'CollisionNode', group: 'Nodes' }, { name: 'CollisionNode', group: 'Nodes' }] };
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    await loadWith(hostWith({ Tree: vi.fn(async () => JSON.stringify(tree)), CollisionSet: set }));
    saveAs(OUT);
    expect((screen.getByRole('button', { name: /Add or replace collision/ }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: /Remove collision/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(set).not.toHaveBeenCalled();
  });

  it('logs a published NIF whose checks failed as a failure, not a success', async () => {
    const appendLog = vi.fn();
    const set = vi.fn(async () => 'RESULT: collision set PUBLISHED WITH FAILED CHECKS (in-process)\nSaved: ' + OUT + '\n[FAIL] no unused strings in the header string table: Stale');
    await loadWith(hostWith({ CollisionSet: set }), appendLog);
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(appendLog).toHaveBeenLastCalledWith(expect.stringContaining('✗ set collision on Scene Root')));
    expect(await screen.findByText(/PUBLISHED WITH FAILED CHECKS/)).toBeTruthy();
  });

  it('shows a failed verification that wrote nothing as an error, not a success', async () => {
    const appendLog = vi.fn();
    const refusal = 'Error: RESULT: collision set FAILED verification; nothing was written';
    await loadWith(hostWith({ CollisionSet: vi.fn(async () => refusal) }), appendLog);
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    expect(await screen.findByText(refusal)).toBeTruthy();
    expect(appendLog).toHaveBeenLastCalledWith(expect.stringContaining('✗ set collision on Scene Root'));
  });

  it('lists each owner of a shared physics system as its own donor body', async () => {
    const shared = { source: 'sha-donor', systems: [
      { block: 9, owner: 2, owned: true, bodyId: 0, node: 'A', constraints: [], unsupported: [], bodies: [donorBodyOf(0, 'static')] },
      { block: 9, owner: 3, owned: true, bodyId: 1, node: 'B', constraints: [], unsupported: [], bodies: [donorBodyOf(0, 'static'), donorBodyOf(1, 'dynamic')] },
    ], findings: [] };
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    const host = hostWith({
      CollisionInfo: vi.fn(async (p: string) => JSON.stringify(p === DONOR ? shared : info)),
      CollisionSet: set,
    });
    await loadWith(host);
    fireEvent.change(screen.getByLabelText('Source'), { target: { value: 'donor' } });
    fireEvent.change(screen.getByLabelText('Donor NIF'), { target: { value: DONOR } });
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    const picker = await screen.findByLabelText('Donor body');
    expect(screen.getByRole('option', { name: /node A/ })).toBeTruthy();
    expect(screen.getByRole('option', { name: /node B/ })).toBeTruthy();
    fireEvent.change(picker, { target: { value: '1' } });
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'donor_nif', DONOR, '', 0, 1, false, 'sha-source', 'sha-donor'));
  });

  it('refuses a node tree read from another snapshot than the collision info', async () => {
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    const host = hostWith({
      Tree: vi.fn(async () => JSON.stringify({ source: 'sha-other', blocks: [{ name: 'Scene Root', group: 'Nodes' }] })),
      CollisionSet: set,
    });
    render(<Collision nif={host} appendLog={() => {}} />, { wrapper: DialogProvider });
    fireEvent.change(screen.getByLabelText('NIF path'), { target: { value: SOURCE } });
    fireEvent.click(screen.getByRole('button', { name: /Load collision/ }));
    expect(await screen.findByText(/changed while it was being read/)).toBeTruthy();
    expect((screen.getByRole('button', { name: /Add or replace collision/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(set).not.toHaveBeenCalled();
  });

  it('clears the loaded node when a reload of the same path fails and needs a fresh load before writing', async () => {
    let fail = false;
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    const host = hostWith({
      CollisionInfo: vi.fn(async () => JSON.stringify(fail ? { error: 'disk read failed' } : info)),
      CollisionSet: set,
    });
    await loadWith(host);
    saveAs(OUT);
    fail = true;
    fireEvent.click(screen.getByRole('button', { name: /Load collision/ }));
    expect(await screen.findByText(/Could not read collision: disk read failed/)).toBeTruthy();
    expect((screen.getByRole('button', { name: /Add or replace collision/ }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: /Remove collision/ }) as HTMLButtonElement).disabled).toBe(true);
    fail = false;
    fireEvent.click(screen.getByRole('button', { name: /Load collision/ }));
    await screen.findByText(/Static body on layer 0/);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'kind', '', 'box', 0, undefined, false, 'sha-source', ''));
  });

  it('refuses a hull mass that is positive but rounds to zero as a float', async () => {
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    await loadWith(hostWith({ CollisionSet: set }));
    fireEvent.change(screen.getByLabelText('Shape'), { target: { value: 'hull' } });
    fireEvent.change(screen.getByLabelText('Mass in kg'), { target: { value: '1e-50' } });
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    expect(await screen.findByText(/Mass must be a number of kilograms a body can store/)).toBeTruthy();
    expect(set).not.toHaveBeenCalled();
  });

  it('reloads the donor list on blur for the same path and sends the reloaded donor source', async () => {
    let donorNow: unknown = donorInfo;
    const set = vi.fn(async () => 'RESULT: collision set OK (in-process)');
    const host = hostWith({
      CollisionInfo: vi.fn(async (p: string) => JSON.stringify(p === DONOR ? donorNow : info)),
      CollisionSet: set,
    });
    await loadWith(host);
    fireEvent.change(screen.getByLabelText('Source'), { target: { value: 'donor' } });
    fireEvent.change(screen.getByLabelText('Donor NIF'), { target: { value: DONOR } });
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    const picker = await screen.findByLabelText('Donor body');
    fireEvent.change(picker, { target: { value: '1' } });
    donorNow = { ...donorInfo, source: 'sha-donor-2', systems: [donorInfo.systems[2], donorInfo.systems[0]] };
    fireEvent.blur(screen.getByLabelText('Donor NIF'));
    await waitFor(() => expect(screen.getByRole('option', { name: /^0: node Tail/ })).toBeTruthy());
    expect((screen.getByLabelText('Donor body') as HTMLSelectElement).value).toBe('0');
    saveAs(OUT);
    fireEvent.click(screen.getByRole('button', { name: /Add or replace collision/ }));
    await waitFor(() => expect(set).toHaveBeenCalledWith(SOURCE, 'Scene Root', OUT, 'donor_nif', DONOR, '', 0, 0, false, 'sha-source', 'sha-donor-2'));
  });
});
