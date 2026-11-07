import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { DialogProvider } from './dialogs';
import NifGltfImport from './NifGltfImport';

const importGltf = vi.fn<(gltf: string, out: string, material: string, texD: string, texN: string,
  collision: boolean, skeleton: string, overwrite: boolean) => Promise<string>>();

const GLTF = 'C:\\in\\thing.glb';
const OUT = 'C:\\out\\thing.nif';

function renderImport() {
  const onResult = vi.fn();
  const appendLog = vi.fn();
  render(<NifGltfImport gltfPath={GLTF} outNif={OUT} skeleton={''} material={''} texDiffuse={''} texNormal={''}
    collision={false} onResult={onResult} appendLog={appendLog} />, { wrapper: DialogProvider });
  return { onResult, appendLog };
}

beforeEach(() => {
  vi.clearAllMocks();
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: { nif: { ImportGltf: importGltf } },
    },
  });
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NifGltfImport', () => {
  // Detects: the import button must pass the glTF path, the output path and an unchecked overwrite to the host, or the import writes nowhere.
  it('imports the glTF to the chosen output path', async () => {
    importGltf.mockResolvedValue('RESULT: import glTF OK (in-process)\nSaved: C:\\out\\thing.nif');
    const { onResult, appendLog } = renderImport();

    fireEvent.click(screen.getByRole('button', { name: 'Import glTF' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('RESULT: import glTF OK (in-process)\nSaved: C:\\out\\thing.nif'));
    expect(importGltf).toHaveBeenCalledWith(GLTF, OUT, '', '', '', false, '', false);
    expect(appendLog).toHaveBeenCalledWith(expect.stringContaining('import thing.glb'));
  });

  // Detects: the import button must not overwrite an existing .nif without asking, and must re-run the import with overwrite once the user agrees.
  it('replaces an existing NIF only after the user confirms the overwrite', async () => {
    importGltf
      .mockResolvedValueOnce('EXISTS: Refusing to replace an existing file without overwrite: C:\\out\\thing.nif')
      .mockResolvedValueOnce('RESULT: import glTF OK (in-process)');
    const { onResult } = renderImport();

    fireEvent.click(screen.getByRole('button', { name: 'Import glTF' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Overwrite' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('RESULT: import glTF OK (in-process)'));
    expect(importGltf).toHaveBeenNthCalledWith(1, GLTF, OUT, '', '', '', false, '', false);
    expect(importGltf).toHaveBeenNthCalledWith(2, GLTF, OUT, '', '', '', false, '', true);
  });

  // Detects: cancelling the overwrite prompt must leave the existing .nif in place, not replace it.
  it('leaves an existing NIF alone when the overwrite is cancelled', async () => {
    importGltf.mockResolvedValue('EXISTS: Refusing to replace an existing file without overwrite: C:\\out\\thing.nif');
    const { onResult } = renderImport();

    fireEvent.click(screen.getByRole('button', { name: 'Import glTF' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Cancel' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('Import cancelled -- thing.nif was left unchanged.'));
    expect(importGltf).toHaveBeenCalledTimes(1);
  });

  // Detects: a refused glTF import must show its reason in the output, not leave an earlier result on screen.
  it('shows a host refusal in the output instead of the previous result', async () => {
    importGltf.mockRejectedValue(new Error('glTF primitive Body is skinned, but no skeleton NIF was chosen'));
    const { onResult } = renderImport();

    fireEvent.click(screen.getByRole('button', { name: 'Import glTF' }));

    await waitFor(() => expect(onResult).toHaveBeenCalledWith('Error: glTF primitive Body is skinned, but no skeleton NIF was chosen'));
  });
});
