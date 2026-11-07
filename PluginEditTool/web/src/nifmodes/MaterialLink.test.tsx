import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { NifHost } from '../backend';
import type { ConfirmRequest } from '../useDialogs';
import MaterialLink from './MaterialLink';

const dialogs = vi.hoisted(() => ({ confirm: vi.fn<(req: ConfirmRequest) => Promise<boolean>>() }));
vi.mock('../useDialogs', () => ({ useDialogs: () => ({ confirm: dialogs.confirm }) }));

const NIF = 'C:\\mods\\a.nif';
const MATERIAL = 'Materials\\Armor\\a.bgsm';

interface Host {
  MaterialCompare: ReturnType<typeof vi.fn>;
  MaterialTextureEdits: ReturnType<typeof vi.fn>;
  MaterialShapes: ReturnType<typeof vi.fn>;
  ApplyEdits: ReturnType<typeof vi.fn>;
}

function makeHost(): Host {
  return {
    MaterialCompare: vi.fn(),
    MaterialTextureEdits: vi.fn(),
    MaterialShapes: vi.fn(),
    ApplyEdits: vi.fn(),
  };
}

function renderLink(host: Host, appendLog = vi.fn()) {
  render(<MaterialLink nif={host as unknown as NifHost} nifPath={NIF} appendLog={appendLog} />);
  fireEvent.change(screen.getByPlaceholderText('Block id'), { target: { value: '4' } });
  return { appendLog };
}

beforeEach(() => {
  dialogs.confirm.mockReset();
});

afterEach(() => {
  cleanup();
});

describe('MaterialLink compare', () => {
  // regression: NIF and material values are swapped, or a differing field is dropped from the compare table.
  it('shows each mismatch with its NIF value and its material value', async () => {
    const host = makeHost();
    host.MaterialCompare.mockResolvedValue(JSON.stringify({
      shape: 4, material: MATERIAL, found: true, format: 'BGSM',
      mismatches: [{ field: 'Alpha', nif: '0.5', material: '0.9' }],
    }));
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Compare/ }));
    const table = await screen.findByRole('table');
    expect(host.MaterialCompare).toHaveBeenCalledWith(NIF, 4, '');
    expect(within(table).getByText('Alpha')).toBeTruthy();
    expect(within(table).getByText('0.5')).toBeTruthy();
    expect(within(table).getByText('0.9')).toBeTruthy();
  });

  // regression: a missing material file is shown as a failed compare instead of a finding.
  it('reports a missing material file as a finding, not an error', async () => {
    const host = makeHost();
    host.MaterialCompare.mockResolvedValue(JSON.stringify({
      shape: 4, material: 'Materials\\gone.bgsm', found: false, format: '', mismatches: [],
    }));
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Compare/ }));
    expect(await screen.findByText('Material file not found: Materials\\gone.bgsm')).toBeTruthy();
    expect(screen.queryByText(/Failed:/)).toBeNull();
  });

  // regression: an error from the native service is swallowed and the panel shows an empty result.
  it('shows an RPC error in the status line', async () => {
    const host = makeHost();
    host.MaterialCompare.mockResolvedValue(JSON.stringify({ error: 'NIF not found: C:\\mods\\a.nif' }));
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Compare/ }));
    expect(await screen.findByText('Failed: NIF not found: C:\\mods\\a.nif')).toBeTruthy();
  });
});

describe('MaterialLink fill', () => {
  const plan = JSON.stringify({
    shape: 4, material: MATERIAL, found: true,
    edits: [{ id: 4, key: 'tex0', value: 'textures\\armor\\a_d.dds' }],
  });

  // regression: fill writes the NIF without asking, or writes edits that the compare did not return.
  it('writes only the returned edits after the user confirms', async () => {
    const host = makeHost();
    host.MaterialTextureEdits.mockResolvedValue(plan);
    host.ApplyEdits.mockResolvedValue('set OK (in-process)\nSaved: C:\\mods\\a.nif');
    host.MaterialCompare.mockResolvedValue(JSON.stringify({
      shape: 4, material: MATERIAL, found: true, format: 'BGSM', mismatches: [],
    }));
    dialogs.confirm.mockResolvedValue(true);
    const { appendLog } = renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Fill texture slots/ }));
    await waitFor(() => expect(host.ApplyEdits).toHaveBeenCalledTimes(1));
    expect(dialogs.confirm).toHaveBeenCalledTimes(1);
    expect(host.ApplyEdits).toHaveBeenCalledWith(NIF, JSON.stringify(JSON.parse(plan).edits), '');
    expect(appendLog).toHaveBeenCalledWith(expect.stringContaining('material fill on shape 4'));
  });

  // regression: a declined fill still overwrites the NIF.
  it('writes nothing when the confirmation is declined', async () => {
    const host = makeHost();
    host.MaterialTextureEdits.mockResolvedValue(plan);
    dialogs.confirm.mockResolvedValue(false);
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Fill texture slots/ }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    expect(host.ApplyEdits).not.toHaveBeenCalled();
  });

  // regression: fill rewrites a file whose texture slots already match the material.
  it('does nothing when the texture slots already match', async () => {
    const host = makeHost();
    host.MaterialTextureEdits.mockResolvedValue(JSON.stringify({ shape: 4, material: MATERIAL, found: true, edits: [] }));
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Fill texture slots/ }));
    expect(await screen.findByText('Texture slots already match the material.')).toBeTruthy();
    expect(dialogs.confirm).not.toHaveBeenCalled();
    expect(host.ApplyEdits).not.toHaveBeenCalled();
  });

  // Detects: the confirm hides that an empty material slot will clear a NIF texture path, so the user approves a silent erase.
  it('says how many slots the fill will clear', async () => {
    const host = makeHost();
    host.MaterialTextureEdits.mockResolvedValue(JSON.stringify({
      shape: 4, material: MATERIAL, found: true,
      edits: [{ id: 4, key: 'tex0', value: 'textures\\armor\\a_d.dds' }, { id: 4, key: 'tex2', value: '' }],
    }));
    dialogs.confirm.mockResolvedValue(false);
    renderLink(host);
    fireEvent.click(screen.getByRole('button', { name: /Fill texture slots/ }));
    await waitFor(() => expect(dialogs.confirm).toHaveBeenCalledTimes(1));
    const { message } = dialogs.confirm.mock.calls[0][0];
    expect(message).toContain('Write 2 texture slot(s)');
    expect(message).toContain('1 of them will be cleared');
  });
});

// regression: the shape list cannot select a block for compare, so the user must type the id by hand.
it('lists the shapes that use a material and picks one for compare', async () => {
  const host = makeHost();
  host.MaterialShapes.mockResolvedValue(JSON.stringify({ material: MATERIAL, shapes: [{ block: 7, name: 'Shape A' }] }));
  render(<MaterialLink nif={host as unknown as NifHost} nifPath={NIF} appendLog={vi.fn()} />);
  fireEvent.change(screen.getByPlaceholderText('Materials\\...\\thing.bgsm'), { target: { value: MATERIAL } });
  fireEvent.click(screen.getByRole('button', { name: 'Find shapes' }));
  fireEvent.click(await screen.findByRole('button', { name: 'Block 7 · Shape A' }));
  expect((screen.getByPlaceholderText('Block id') as HTMLInputElement).value).toBe('7');
  expect(host.MaterialShapes).toHaveBeenCalledWith(NIF, MATERIAL);
});
