import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { DialogProvider } from './dialogs';
import NifGltfExport from './NifGltfExport';

const browse = vi.fn<(title: string, filter: string) => Promise<string>>();
const exportGltf = vi.fn<(nifPath: string, outGlb: string, overwrite: boolean, materials: boolean, textures: string, tangents: boolean, vertexColours: boolean) => Promise<string>>();

function renderExport() {
  const onResult = vi.fn();
  const appendLog = vi.fn();
  render(<NifGltfExport nifPath={'C:\\mod\\thing.nif'} onResult={onResult} appendLog={appendLog} />, { wrapper: DialogProvider });
  return { onResult, appendLog };
}

beforeEach(() => {
  vi.clearAllMocks();
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: { nif: { BrowseForSave: browse, ExportGltf: exportGltf } },
    },
  });
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NifGltfExport', () => {
  it('exports to the path the save dialog returns', async () => {
    // Regression: the export button did not pass the NIF path and the chosen .glb path to the host.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf.mockResolvedValue('RESULT: gltf export OK\nSaved: C:\\out\\thing.glb');
    const { onResult, appendLog } = renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('RESULT: gltf export OK\nSaved: C:\\out\\thing.glb'));
    expect(browse).toHaveBeenCalledWith('Export glTF as', 'glTF binary (*.glb)|*.glb');
    expect(exportGltf).toHaveBeenCalledWith('C:\\mod\\thing.nif', 'C:\\out\\thing.glb', false, true, 'png', true, true);
    expect(appendLog).toHaveBeenCalledWith(expect.stringContaining('export thing.nif'));
  });

  it('replaces an existing glTF only after the user confirms the overwrite', async () => {
    // Detects: the export button overwriting an existing .glb without asking, or not re-running the export with overwrite after the user agrees.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf
      .mockResolvedValueOnce('EXISTS: Refusing to replace an existing file without overwrite: C:\\out\\thing.glb')
      .mockResolvedValueOnce('RESULT: gltf export OK\nSaved: C:\\out\\thing.glb');
    const { onResult } = renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Overwrite' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('RESULT: gltf export OK\nSaved: C:\\out\\thing.glb'));
    expect(exportGltf).toHaveBeenNthCalledWith(1, 'C:\\mod\\thing.nif', 'C:\\out\\thing.glb', false, true, 'png', true, true);
    expect(exportGltf).toHaveBeenNthCalledWith(2, 'C:\\mod\\thing.nif', 'C:\\out\\thing.glb', true, true, 'png', true, true);
  });

  it('leaves an existing glTF alone when the overwrite is cancelled', async () => {
    // Detects: cancelling the overwrite prompt still replacing the existing .glb.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf.mockResolvedValue('EXISTS: Refusing to replace an existing file without overwrite: C:\\out\\thing.glb');
    const { onResult } = renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Cancel' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('Export cancelled -- thing.glb was left unchanged.'));
    expect(exportGltf).toHaveBeenCalledTimes(1);
  });

  it('does not export when the save dialog is cancelled', async () => {
    // Regression: a cancelled save dialog still asked the host to export to an empty path.
    browse.mockResolvedValue('');
    const { onResult } = renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));

    await waitFor(() => expect(browse).toHaveBeenCalled());
    expect(exportGltf).not.toHaveBeenCalled();
    expect(onResult).not.toHaveBeenCalled();
  });

  it('shows a host failure in the output instead of the previous result', async () => {
    // Regression: a rejected export left the output panel on an earlier result with no error shown.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf.mockRejectedValue(new Error('NIF not found'));
    const { onResult } = renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('Error: NIF not found'));
  });

  it('sends the options the user picked, so unticking materials must reach the host as false', async () => {
    // Detects: the export dialog that must send its materials, texture mode, tangent and vertex colour choices to the host, or send the defaults whatever the user picked.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf.mockResolvedValue('RESULT: gltf export OK\nSaved: C:\\out\\thing.glb');
    renderExport();

    fireEvent.click(screen.getByRole('checkbox', { name: 'Materials and textures' }));
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'embed' } });
    fireEvent.click(screen.getByRole('checkbox', { name: 'Tangents' }));
    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));

    await waitFor(() => expect(exportGltf).toHaveBeenCalledTimes(1));
    expect(exportGltf).toHaveBeenCalledWith('C:\\mod\\thing.nif', 'C:\\out\\thing.glb', false, false, 'embed', false, true);
    expect(screen.getByRole<HTMLSelectElement>('combobox').disabled).toBe(true);
  });

  it('names the texture file that blocks the export in the overwrite prompt', async () => {
    // Detects: a texture collision that must name the texture file in the overwrite prompt, not the .glb the user did not see change.
    browse.mockResolvedValue('C:\\out\\thing.glb');
    exportGltf.mockResolvedValue('EXISTS: Refusing to replace an existing file without overwrite: C:\\out\\textures\\armor\\a_d.dds');
    renderExport();

    fireEvent.click(screen.getByRole('button', { name: 'Export glTF…' }));

    await screen.findByText('a_d.dds already exists. Overwrite it with the export of thing.nif?');
    expect(exportGltf).toHaveBeenCalledTimes(1);
  });
});
