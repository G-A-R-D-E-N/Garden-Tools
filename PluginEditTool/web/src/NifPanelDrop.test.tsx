import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import NifPanel from './NifPanel';

vi.mock('./NifViewport', () => ({ default: () => null }));
vi.mock('./NifEditor', () => ({ default: () => null }));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: vi.fn() }) }));

const stage = vi.fn<(name: string, base64: string) => Promise<string>>();
const geo = vi.fn<(path: string) => Promise<string>>();
const tree = vi.fn<(path: string) => Promise<string>>();
const inspect = vi.fn<(path: string) => Promise<string>>();
const importNif = vi.fn<(objPath: string, outNif: string, material: string, texDiffuse: string,
  texNormal: string, collision: boolean, fromBlender: boolean) => Promise<string>>();

function stubBridge() {
  vi.stubGlobal('chrome', {
    webview: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      hostObjects: {
        nif: { StageDroppedFile: stage, Geo: geo, Tree: tree, Import: importNif },
        material: { Inspect: inspect },
      },
    },
  });
}

function dropZone() {
  const zone = document.querySelector('.papyrus-drop');
  if (!zone) throw new Error('no drop zone rendered');
  return zone;
}

function drop(name: string) {
  fireEvent.drop(dropZone(), { dataTransfer: { files: [new File(['payload'], name)] } });
}

function runLabel() {
  return document.querySelector('.papyrus-run')?.textContent ?? '';
}

function fieldValue(pattern: RegExp) {
  return (screen.getByPlaceholderText(pattern) as HTMLInputElement).value;
}

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  stage.mockImplementation(async (name: string) => 'C:\\staged\\' + name);
  geo.mockResolvedValue(JSON.stringify({ shapes: [] }));
  tree.mockResolvedValue(JSON.stringify({ blocks: [] }));
  inspect.mockResolvedValue(JSON.stringify({ fields: [] }));
  importNif.mockResolvedValue('RESULT: import OK');
  stubBridge();
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe('NIF drop routing', () => {
  it('loads a dropped .nif through the staged path in view mode', async () => {
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.nif');
    await waitFor(() => expect(geo).toHaveBeenCalledWith('C:\\staged\\mesh.nif'));
    expect(stage).toHaveBeenCalledWith('mesh.nif', expect.any(String));
    expect(fieldValue(/drag a \.nif here/)).toBe('C:\\staged\\mesh.nif');
  });

  it('loads a dropped .nif as a tree in edit mode', async () => {
    localStorage.setItem('nif.mode', 'edit');
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.nif');
    await waitFor(() => expect(tree).toHaveBeenCalledWith('C:\\staged\\mesh.nif'));
    await waitFor(() => expect(geo).toHaveBeenCalledWith('C:\\staged\\mesh.nif'));
  });

  it('sends a dropped .nif in import mode to inspect', async () => {
    localStorage.setItem('nif.mode', 'import');
    render(<NifPanel onClose={() => {}} />);
    expect(runLabel()).toContain('Author NIF');
    drop('mesh.nif');
    await waitFor(() => expect(runLabel()).toContain('Inspect'));
    expect(geo).not.toHaveBeenCalled();
    expect(tree).not.toHaveBeenCalled();
  });

  it('loads a dropped material in materials mode', async () => {
    localStorage.setItem('nif.mode', 'materials');
    render(<NifPanel onClose={() => {}} />);
    drop('skin.bgsm');
    await waitFor(() => expect(inspect).toHaveBeenCalledWith('C:\\staged\\skin.bgsm'));
    expect(fieldValue(/drag a \.bgsm/)).toBe('C:\\staged\\skin.bgsm');
  });

  it('routes a dropped .obj to the import path', async () => {
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.obj');
    await waitFor(() => expect(runLabel()).toContain('Author NIF'));
    expect(fieldValue(/drag a \.obj here/)).toBe('C:\\staged\\mesh.obj');
    expect(geo).not.toHaveBeenCalled();
  });

  it('loads the staged path, not the path typed before the drop', async () => {
    render(<NifPanel onClose={() => {}} />);
    fireEvent.change(screen.getByPlaceholderText(/drag a \.nif here/), { target: { value: 'typed.nif' } });
    drop('dropped.nif');
    await waitFor(() => expect(geo).toHaveBeenCalledWith('C:\\staged\\dropped.nif'));
    expect(geo).not.toHaveBeenCalledWith('typed.nif');
    expect(fieldValue(/drag a \.nif here/)).toBe('C:\\staged\\dropped.nif');
  });
});

describe('NIF drop failures', () => {
  it('reports a staged drop the host rejected', async () => {
    localStorage.setItem('nif.mode', 'inspect');
    stage.mockResolvedValue('ERR: unsupported file');
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.nif');
    await screen.findByText(/drop failed -- ERR: unsupported file/);
    expect(geo).not.toHaveBeenCalled();
    expect(tree).not.toHaveBeenCalled();
  });

  it('reports a rejected drop with the host message', async () => {
    localStorage.setItem('nif.mode', 'inspect');
    stage.mockRejectedValue(new Error('stage exploded'));
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.nif');
    await screen.findByText(/drop failed -- stage exploded/);
  });

  it('logs a successful drop', async () => {
    localStorage.setItem('nif.mode', 'inspect');
    render(<NifPanel onClose={() => {}} />);
    drop('mesh.nif');
    await screen.findByText(/dropped mesh\.nif/);
  });
});

describe('NIF import box collision', () => {
  function openImport() {
    localStorage.setItem('nif.mode', 'import');
    localStorage.setItem('nif.objPath', 'C:\\mods\\quad.obj');
    localStorage.setItem('nif.outNif', 'C:\\mods\\quad.nif');
    render(<NifPanel onClose={() => {}} />);
  }

  it('leaves box collision off and sends collision false by default', async () => {
    // Regression: the box collision option defaults on, so every OBJ import gains a collision body the user did not ask for.
    openImport();
    expect((screen.getByLabelText(/Add box collision/) as HTMLInputElement).checked).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: /Author NIF/ }));
    await waitFor(() => expect(importNif).toHaveBeenCalledWith(
      'C:\\mods\\quad.obj', 'C:\\mods\\quad.nif', '', '', '', false, false));
  });

  it('sends collision true when the box is checked', async () => {
    // Regression: the checked box is not passed to Import, so the NIF is written without the collision the user asked for.
    openImport();
    fireEvent.click(screen.getByLabelText(/Add box collision/));
    fireEvent.click(screen.getByRole('button', { name: /Author NIF/ }));
    await waitFor(() => expect(importNif).toHaveBeenCalledWith(
      'C:\\mods\\quad.obj', 'C:\\mods\\quad.nif', '', '', '', true, false));
  });
});
