import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from '../backend';
import { DialogProvider } from '../dialogs';
import Segments from './Segments';

const PATH = 'C:\\mods\\armor.nif';

const tree = JSON.stringify({
  fo4: true,
  file: 'armor.nif',
  blocks: [
    { id: 0, type: 'NiNode', name: 'Root', group: 'Nodes', cat: 'node', fields: [] },
    { id: 4, type: 'BSSubIndexTriShape', name: 'Tri', group: 'Shapes', cat: 'shape', fields: [] },
    { id: 9, type: 'BSSubIndexTriShape', name: 'Arm', group: 'Shapes', cat: 'shape', fields: [] },
  ],
});

function segmentTable(block: number, name: string, cutOffsets: (number | null)[] = [0.5],
                      sidecar: Record<string, unknown> = { path: 'C:\\mods\\Tri.ssf', found: false, listed: false }) {
  return JSON.stringify({
    block,
    name,
    primitives: 2,
    totalSegments: 3,
    segments: [{
      startIndex: 0,
      primitives: 1,
      parentArrayIndex: 0,
      subSegments: [{ startIndex: 0, primitives: 1, parentArrayIndex: 0, unused: 7 }],
    }],
    segmentStarts: [0],
    userData: [{ userIndex: 0, boneId: 9, cutOffsets }],
    ssfFile: 'Tri.ssf',
    sidecar,
  });
}

function fakeHost(overrides: Partial<Record<keyof NifHost, unknown>> = {}) {
  return {
    Tree: vi.fn(async () => tree),
    Segments: vi.fn(async (_path: string, block: number) => segmentTable(block, 'Tri')),
    ApplyEdits: vi.fn(async () => 'set OK (in-process)\nSaved: C:\\mods\\armor.nif'),
    ...overrides,
  } as unknown as NifHost & { Segments: ReturnType<typeof vi.fn>; ApplyEdits: ReturnType<typeof vi.fn> };
}

function renderPanel(host: NifHost, appendLog = vi.fn()) {
  return render(<DialogProvider><Segments nif={host} nifPath={PATH} appendLog={appendLog} /></DialogProvider>);
}

async function loadSegments(host: NifHost) {
  renderPanel(host);
  fireEvent.click(screen.getByRole('button', { name: /Load segments/ }));
  return screen.findAllByLabelText('Start index');
}

afterEach(() => cleanup());

describe('Segments mode', () => {
  // Detects: a NiNode or other block listed as a segment table, so the user could pick a block with no segment data.
  it('lists only BSSubIndexTriShape blocks and reads the first one', async () => {
    const host = fakeHost();
    await loadSegments(host);

    const options = within(screen.getByRole('combobox')).getAllByRole('option').map(option => option.textContent);
    expect(options).toEqual(['4 Tri', '9 Arm']);
    expect(host.Segments).toHaveBeenCalledWith(PATH, 4);
  });

  // Detects: a segment edit sent under a different key, or with the read-only block, name and sidecar fields, which the writer does not take.
  it('sends the edited table through the segments key with only the writer fields', async () => {
    const host = fakeHost();
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.click(screen.getByRole('button', { name: /Save segments/ }));

    await waitFor(() => expect(host.ApplyEdits).toHaveBeenCalledTimes(1));
    const [path, payload, outNif] = host.ApplyEdits.mock.calls[0] as [string, string, string];
    expect(path).toBe(PATH);
    expect(outNif).toBe('');
    const [edit] = JSON.parse(payload) as { id: number; key: string; value: Record<string, unknown> }[];
    expect(edit.id).toBe(4);
    expect(edit.key).toBe('segments');
    expect(edit.value.segments).toEqual([expect.objectContaining({ startIndex: 3 })]);
    expect(Object.keys(edit.value).sort()).toEqual(
      ['primitives', 'segmentStarts', 'segments', 'ssfFile', 'totalSegments', 'userData']);
  });

  // Detects: a fractional or blank number sent to the writer, which would be stored truncated or refused after the file was touched.
  it('keeps Save disabled for a value that is not a whole number', async () => {
    const host = fakeHost();
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '1.5' } });

    expect((screen.getByRole('button', { name: /Save segments/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(screen.getByText(/whole number from 0 to 4294967295/)).toBeTruthy();
    expect(host.ApplyEdits).not.toHaveBeenCalled();
  });

  // Detects: a refused write reported as saved, or the user's edit dropped, so the refusal reason must show and the edit must stay.
  it('shows the writer refusal and keeps the edited value', async () => {
    const appendLog = vi.fn();
    const host = fakeHost({
      ApplyEdits: vi.fn(async () => { throw new Error('nif.ApplyEdits: NifEditFailed: key segments refused'); }),
    });
    renderPanel(host, appendLog);
    fireEvent.click(screen.getByRole('button', { name: /Load segments/ }));
    const inputs = await screen.findAllByLabelText('Start index');
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.click(screen.getByRole('button', { name: /Save segments/ }));

    expect(await screen.findByText(/NifEditFailed: key segments refused/)).toBeTruthy();
    expect(appendLog).toHaveBeenCalledWith(expect.stringContaining('segments save failed'));
    expect((screen.getAllByLabelText('Start index')[0] as HTMLInputElement).value).toBe('3');
  });

  // Detects: a successful write shown from the draft instead of re-read from the NIF, so the panel would hide what the file really holds.
  it('re-reads the table from the NIF after a successful write', async () => {
    const host = fakeHost();
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.click(screen.getByRole('button', { name: /Save segments/ }));

    await waitFor(() => expect(host.Segments).toHaveBeenCalledTimes(2));
    expect(await screen.findByText(/Saved to the NIF in place/)).toBeTruthy();
  });

  // Detects: the panel presenting an edited table as proven to load in game; the unproven note must show before and after a save.
  it('states that game acceptance of an edited segment table is unproven', async () => {
    const host = fakeHost();
    await loadSegments(host);

    expect(screen.getAllByText(/Game acceptance of a rewritten segment table is unproven/).length).toBeGreaterThan(0);
  });

  // Detects: Save As overwriting the open NIF in place, or dropping the path the user picked; the copy must go to the picked file and the open file must not be reloaded.
  it('saves a copy to the path picked with Save As', async () => {
    const host = fakeHost({ BrowseForSave: vi.fn(async () => 'C:\\mods\\copy.nif') });
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.click(screen.getByRole('button', { name: /Save As/ }));

    await waitFor(() => expect(host.ApplyEdits).toHaveBeenCalledTimes(1));
    const [path, , outNif] = host.ApplyEdits.mock.calls[0] as [string, string, string];
    expect(path).toBe(PATH);
    expect(outNif).toBe('C:\\mods\\copy.nif');
    expect(await screen.findByText(/Saved a copy to/)).toBeTruthy();
    expect(host.Segments).toHaveBeenCalledTimes(1);
  });

  // Detects: a null cut offset from a corrupt NIF joining as an empty string, so Save would drop it and change the list length; Save must stay disabled and say why.
  it('refuses to save a table with a cut offset that is not a finite number', async () => {
    const host = fakeHost({ Segments: vi.fn(async (_path: string, block: number) => segmentTable(block, 'Tri', [0.5, null])) });
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });

    expect((screen.getByRole('button', { name: /Save segments/ }) as HTMLButtonElement).disabled).toBe(true);
    expect(screen.getByText(/not a finite number, so saving would drop it/)).toBeTruthy();
    expect(host.ApplyEdits).not.toHaveBeenCalled();
  });

  // Detects: switching block silently discarding unsaved edits; Cancel must keep the edits and leave the first block loaded.
  it('keeps unsaved edits when the block switch is cancelled', async () => {
    const host = fakeHost();
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.change(screen.getByRole('combobox'), { target: { value: '9' } });

    expect(await screen.findByText('Discard unsaved edits?')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));

    await waitFor(() => expect(screen.queryByText('Discard unsaved edits?')).toBeNull());
    expect(host.Segments).toHaveBeenCalledTimes(1);
    expect((screen.getAllByLabelText('Start index')[0] as HTMLInputElement).value).toBe('3');
  });

  // Detects: a confirmed block switch that does not load the chosen block; Discard must read the newly selected block.
  it('loads the chosen block when the unsaved edits are discarded', async () => {
    const host = fakeHost();
    const inputs = await loadSegments(host);
    fireEvent.change(inputs[0], { target: { value: '3' } });
    fireEvent.change(screen.getByRole('combobox'), { target: { value: '9' } });

    fireEvent.click(await screen.findByRole('button', { name: 'Discard' }));

    await waitFor(() => expect(host.Segments).toHaveBeenLastCalledWith(PATH, 9));
  });

  // Detects: a sidecar read error that the reader reports but the panel hides, so the user would see only a found sidecar; the error must show beside the table.
  it('shows a sidecar read error beside the table', async () => {
    const host = fakeHost({
      Segments: vi.fn(async (_path: string, block: number) => segmentTable(block, 'Tri', [0.5],
        { path: 'C:\\mods\\Tri.ssf', found: true, listed: false, error: 'ssf root must be an object' })),
    });
    await loadSegments(host);

    expect(await screen.findByText(/Sidecar could not be read: ssf root must be an object/)).toBeTruthy();
  });
});
