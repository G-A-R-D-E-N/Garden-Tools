import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import NifConnectPoints from './NifConnectPoints';
import type { NifHost } from './backend';

const PATH = 'C:\\mods\\gun.nif';
const OTHER_PATH = 'C:\\mods\\other.nif';

const OTHER_BLOCKS = [
  {
    block: 5, type: 'BSConnectPoint::Parents', name: 'CPB',
    points: [{ parent: '', name: 'X-Other', rotation: [1, 0, 0, 0], translation: [0, 0, 0], scale: 1 }],
  },
];

const BLOCKS = [
  {
    block: 2, type: 'BSConnectPoint::Parents', name: 'CPA',
    points: [
      { parent: 'Chest', name: 'P-Muzzle', rotation: [1, 0, 0, 0], translation: [1, 2, 3], scale: 1 },
      { parent: '', name: 'P-Grip', rotation: [0.5, 0.5, 0.5, 0.5], translation: [-4, 0.25, 8], scale: 2 },
    ],
  },
  { block: 3, type: 'BSConnectPoint::Children', name: 'CPT', skinned: true, names: ['C-Barrel'] },
];

const connectPoints = vi.fn<(path: string) => Promise<string>>();
const applyConnectPoints = vi.fn<(path: string, opsJson: string, outNif: string) => Promise<string>>();

function renderPanel(dirty = false) {
  const nif = { ConnectPoints: connectPoints, ApplyConnectPoints: applyConnectPoints } as unknown as NifHost;
  return render(<NifConnectPoints nif={nif} nifPath={PATH} dirty={dirty} />);
}

function saveButton() {
  return screen.getByRole('button', { name: 'Save' }) as HTMLButtonElement;
}

beforeEach(() => {
  connectPoints.mockReset();
  applyConnectPoints.mockReset();
  connectPoints.mockResolvedValue(JSON.stringify(BLOCKS));
  applyConnectPoints.mockResolvedValue('connect points OK (in-process)\nSaved: C:\\mods\\gun.nif');
});

afterEach(cleanup);

describe('NifConnectPoints', () => {
  it('lists the connect blocks and shows the first block points', async () => {
    renderPanel();
    await screen.findByDisplayValue('P-Muzzle');
    expect(connectPoints).toHaveBeenCalledWith(PATH);
    expect(screen.getByRole('button', { name: /CPA · 2 point/ })).toBeTruthy();
    expect(screen.getByRole('button', { name: /CPT · 1 child/ })).toBeTruthy();
  });

  it('renames a point and saves one set op to the same file', async () => {
    renderPanel();
    const name = await screen.findByDisplayValue('P-Muzzle');
    fireEvent.change(name, { target: { value: 'P-Renamed' } });
    fireEvent.click(saveButton());
    await waitFor(() => expect(applyConnectPoints).toHaveBeenCalledTimes(1));
    expect(applyConnectPoints).toHaveBeenCalledWith(
      PATH,
      JSON.stringify([{ op: 'set', block: 2, index: 0, field: 'name', value: 'P-Renamed' }]),
      PATH,
    );
  });

  it('removes a point from the end of the list', async () => {
    renderPanel();
    await screen.findByDisplayValue('P-Muzzle');
    fireEvent.click(screen.getByLabelText('Remove point 2'));
    fireEvent.click(saveButton());
    await waitFor(() => expect(applyConnectPoints).toHaveBeenCalledTimes(1));
    expect(applyConnectPoints.mock.calls[0][1]).toBe(JSON.stringify([{ op: 'remove', block: 2, index: 1 }]));
  });

  it('adds a point and sends it as an add op', async () => {
    renderPanel();
    await screen.findByDisplayValue('P-Muzzle');
    fireEvent.click(screen.getByRole('button', { name: /Add point/ }));
    const parentFields = screen.getAllByPlaceholderText('empty or WorkshopConnectPoints');
    fireEvent.change(parentFields[2], { target: { value: 'Chest' } });
    fireEvent.click(saveButton());
    await waitFor(() => expect(applyConnectPoints).toHaveBeenCalledTimes(1));
    expect(applyConnectPoints.mock.calls[0][1]).toBe(JSON.stringify([{
      op: 'add', block: 2,
      value: { parent: 'Chest', name: '', rotation: [1, 0, 0, 0], translation: [0, 0, 0], scale: 1 },
    }]));
  });

  it('edits the children block skinned flag and names', async () => {
    renderPanel();
    await screen.findByDisplayValue('P-Muzzle');
    fireEvent.click(screen.getByRole('button', { name: /CPT · 1 child/ }));
    fireEvent.click(screen.getByLabelText(/Skinned/));
    fireEvent.change(screen.getByDisplayValue('C-Barrel'), { target: { value: 'C-Sight' } });
    fireEvent.click(saveButton());
    await waitFor(() => expect(applyConnectPoints).toHaveBeenCalledTimes(1));
    expect(applyConnectPoints.mock.calls[0][1]).toBe(JSON.stringify([
      { op: 'set', block: 3, field: 'skinned', value: false },
      { op: 'set', block: 3, index: 0, field: 'name', value: 'C-Sight' },
    ]));
  });

  it('keeps save disabled while the tree editor has unsaved edits', async () => {
    renderPanel(true);
    const name = await screen.findByDisplayValue('P-Muzzle');
    fireEvent.change(name, { target: { value: 'P-Renamed' } });
    expect(saveButton().disabled).toBe(true);
    expect(screen.getByText('Save or discard the tree edits first.')).toBeTruthy();
    expect(applyConnectPoints).not.toHaveBeenCalled();
  });

  it('shows the host error when the NIF cannot be read', async () => {
    connectPoints.mockResolvedValue(JSON.stringify({ error: 'block 2 is truncated' }));
    renderPanel();
    expect(await screen.findByText(/Could not read connect points: block 2 is truncated/)).toBeTruthy();
  });

  it('reports a refused save and keeps the draft', async () => {
    applyConnectPoints.mockResolvedValue('Error: connect point parent \'Ghost\' is not a node in this NIF');
    renderPanel();
    const name = await screen.findByDisplayValue('P-Muzzle');
    fireEvent.change(name, { target: { value: 'P-Renamed' } });
    fireEvent.click(saveButton());
    expect(await screen.findByText(/is not a node in this NIF/)).toBeTruthy();
    expect(screen.getByDisplayValue('P-Renamed')).toBeTruthy();
  });

  it('ignores a read that finishes after the NIF path changed', async () => {
    let resolveFirst: (value: string) => void = () => {};
    connectPoints
      .mockImplementationOnce(() => new Promise<string>(resolve => { resolveFirst = resolve; }))
      .mockImplementationOnce(async () => JSON.stringify(OTHER_BLOCKS));
    const nif = { ConnectPoints: connectPoints, ApplyConnectPoints: applyConnectPoints } as unknown as NifHost;
    const view = render(<NifConnectPoints nif={nif} nifPath={PATH} dirty={false} />);
    view.rerender(<NifConnectPoints nif={nif} nifPath={OTHER_PATH} dirty={false} />);
    await screen.findByDisplayValue('X-Other');
    await act(async () => { resolveFirst(JSON.stringify(BLOCKS)); });
    expect(screen.getByDisplayValue('X-Other')).toBeTruthy();
    expect(screen.queryByDisplayValue('P-Muzzle')).toBeNull();
  });

  it('reads the connect points again when the reload key changes', async () => {
    const nif = { ConnectPoints: connectPoints, ApplyConnectPoints: applyConnectPoints } as unknown as NifHost;
    const view = render(<NifConnectPoints nif={nif} nifPath={PATH} dirty={false} reloadKey={0} />);
    await screen.findByDisplayValue('P-Muzzle');
    view.rerender(<NifConnectPoints nif={nif} nifPath={PATH} dirty={false} reloadKey={1} />);
    await waitFor(() => expect(connectPoints).toHaveBeenCalledTimes(2));
  });

  it('keeps the selected block when the reload key changes', async () => {
    const nif = { ConnectPoints: connectPoints, ApplyConnectPoints: applyConnectPoints } as unknown as NifHost;
    const view = render(<NifConnectPoints nif={nif} nifPath={PATH} dirty={false} reloadKey={0} />);
    await screen.findByDisplayValue('P-Muzzle');
    fireEvent.click(screen.getByRole('button', { name: /CPT · 1 child/ }));
    let resolveReload: (value: string) => void = () => {};
    connectPoints.mockImplementationOnce(() => new Promise<string>(resolve => { resolveReload = resolve; }));
    view.rerender(<NifConnectPoints nif={nif} nifPath={PATH} dirty={false} reloadKey={1} />);
    await waitFor(() => expect(connectPoints).toHaveBeenCalledTimes(2));
    await act(async () => { resolveReload(JSON.stringify(BLOCKS)); });
    expect(screen.getByLabelText(/Skinned/)).toBeTruthy();
  });
});
