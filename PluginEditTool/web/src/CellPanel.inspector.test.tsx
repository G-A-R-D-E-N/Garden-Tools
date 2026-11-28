import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';
import type { CellPlacedReference } from './backend';

const bridge = vi.hoisted(() => ({
  load: vi.fn<(...args: unknown[]) => Promise<string>>(),
  details: vi.fn<(...args: unknown[]) => Promise<string>>(),
}));

vi.mock('./backend', () => ({
  getCell: () => ({
    GetPlugins: async () => '["Fallout4.esm"]',
    SearchCells: async () => '[]',
    SearchWorldspaces: async () => '[]',
    GetPlacedReferencesAtGrid: bridge.load,
    GetReferenceDetails: bridge.details,
  }),
  getBackend: () => ({}),
}));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: vi.fn() }) }));
vi.mock('./CellViewport', async () => {
  const { createElement } = await import('react');
  return {
    default: (props: {
      references: CellPlacedReference[];
      onSelect: (ref: CellPlacedReference | null) => void;
      onReferenceDoubleClick: (ref: CellPlacedReference) => void;
    }) => createElement(
      'div',
      null,
      createElement('button', { onClick: () => props.onSelect(props.references[0]) }, 'pick reference'),
      createElement('button', { onClick: () => props.onReferenceDoubleClick(props.references[0]) }, 'double click reference'),
    ),
  };
});

const ref = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  recordType: 'REFR',
  baseType: 'STAT',
  baseFormKey: '00012345:Base.esm',
  baseEditorId: 'Rock01',
  modelPath: null,
  position: { x: 1, y: 2, z: 3 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
};

const details = {
  formKey: ref.formKey,
  editorId: 'Ref1',
  signature: 'REFR',
  pluginsWithRecord: [],
  scripts: null,
  baseScripts: null,
};

const withScripts = {
  ...details,
  scripts: {
    version: 6,
    objectFormat: 2,
    unsupported: false,
    fragmentBytes: 0,
    scripts: [{ name: 'MyScript', flags: 0, properties: [] }],
  },
};

const inspectorShown = () => screen.queryByRole('complementary', { name: 'Reference inspector' });
const tabSelected = (name: string) => screen.getByRole('tab', { name }).getAttribute('aria-selected');

async function loadCell() {
  render(<CellPanel onClose={() => {}} />);
  const inputs = screen.getAllByRole('spinbutton') as HTMLInputElement[];
  await waitFor(() => expect(inputs.every(input => !input.disabled)).toBe(true));
  fireEvent.keyDown(inputs[0], { key: 'Enter' });
  await screen.findByRole('button', { name: 'pick reference' });
}

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  localStorage.setItem('cell.pickMode', 'grid');
  localStorage.setItem('cell.worldspace', '00003C:Fallout4.esm');
  localStorage.setItem('cell.gridX', '1');
  localStorage.setItem('cell.gridY', '0');
  bridge.load.mockResolvedValue(JSON.stringify({ references: [ref] }));
  bridge.details.mockResolvedValue(JSON.stringify(details));
});

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('Cell Viewer reference inspector wiring', () => {
  it('a single pick opens the docked inspector on the Reference tab, closes it, and reopens it from the summary', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'pick reference' }));
    expect(await screen.findByText('Identity')).toBeTruthy();
    expect(inspectorShown()).toBeTruthy();
    expect(tabSelected('Reference')).toBe('true');
    expect(screen.getByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Close reference inspector' }));
    expect(inspectorShown()).toBeNull();
    expect(screen.getByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Show details' }));
    expect(inspectorShown()).toBeTruthy();
  });

  it('the selection footer reads Details while open and focuses the open inspector', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'pick reference' }));
    await screen.findByText('Identity');
    const panel = inspectorShown() as HTMLElement;
    fireEvent.click(screen.getByRole('button', { name: 'Details' }));
    expect(inspectorShown()).toBe(panel);
    expect(document.activeElement).toBe(panel);
    expect(tabSelected('Reference')).toBe('true');
  });

  it('the selection footer stays visible on the Layers and Log tabs', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'pick reference' }));
    fireEvent.click(screen.getByRole('button', { name: 'Close reference inspector' }));
    fireEvent.click(screen.getByRole('button', { name: 'Layers' }));
    expect(screen.getByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: /^Log/ }));
    expect(screen.getByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();
  });

  it('keeps the inspector hidden before any reference is picked', async () => {
    await loadCell();
    expect(inspectorShown()).toBeNull();
    expect(screen.queryByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeNull();
  });

  it('a double-click opens the Scripts tab when the reference has scripts', async () => {
    bridge.details.mockResolvedValue(JSON.stringify(withScripts));
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'double click reference' }));
    await waitFor(() => expect(tabSelected('Scripts')).toBe('true'));
    expect(inspectorShown()).toBeTruthy();
    expect(await screen.findByText('MyScript')).toBeTruthy();
  });

  it('a double-click opens the Reference tab when the reference has no scripts', async () => {
    await loadCell();
    fireEvent.click(screen.getByRole('button', { name: 'double click reference' }));
    await waitFor(() => expect(tabSelected('Reference')).toBe('true'));
    expect(inspectorShown()).toBeTruthy();
    expect(await screen.findByText('Identity')).toBeTruthy();
  });
});
