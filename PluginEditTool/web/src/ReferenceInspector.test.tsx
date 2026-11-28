import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import ReferenceInspector, { ReferenceInspectorDock, SelectedSummary, type EditHandlers } from './ReferenceInspector';
import { useReferenceInspector } from './useReferenceInspector';
import type { CellPlacedReference } from './backend';

const rpc = vi.hoisted(() => ({
  cell: {} as Record<string, unknown>,
}));

vi.mock('./backend', () => ({
  getBackend: () => ({}),
  getCell: () => rpc.cell,
}));

const reference = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  baseType: 'STAT',
  recordType: 'REFR',
  baseEditorId: 'Rock01',
  baseFormKey: '00012345:Base.esm',
  position: { x: 1, y: 2, z: 3 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1,
} as CellPlacedReference;

const details = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  signature: 'REFR',
  pluginsWithRecord: [],
  scripts: null,
  baseScripts: null,
};

const edit = (): EditHandlers => ({
  patchChosen: false,
  canUndo: false,
  canRedo: false,
  onEdit: vi.fn(async () => true),
  onDelete: vi.fn(),
  onDuplicate: vi.fn(),
  onUndoEdit: vi.fn(),
  onRedoEdit: vi.fn(),
});

function Harness({ selected, onDeselect, onHide }: {
  selected: CellPlacedReference | null;
  onDeselect: () => void;
  onHide: () => void;
}) {
  const inspector = useReferenceInspector(1600);
  return (
    <>
      <button onClick={() => inspector.show('reference')}>open</button>
      <ReferenceInspectorDock
        selected={selected}
        inspector={inspector}
        refRevision={0}
        viewportWidth={1600}
        onHide={onHide}
        onDeselect={onDeselect}
        edit={edit()}
      />
    </>
  );
}

const inspectorShown = () => screen.queryByRole('complementary', { name: 'Reference inspector' });

beforeEach(() => {
  localStorage.clear();
  rpc.cell = { GetReferenceDetails: vi.fn(async () => JSON.stringify(details)) };
});

afterEach(() => {
  cleanup();
  localStorage.clear();
});

describe('ReferenceInspectorDock', () => {
  it('renders nothing without a selection', () => {
    render(
      <Harness selected={null} onDeselect={vi.fn()} onHide={vi.fn()} />,
    );
    expect(inspectorShown()).toBeNull();
    expect(rpc.cell.GetReferenceDetails).not.toHaveBeenCalled();
  });

  it('stays hidden until the inspector is opened, then opens on the Reference tab', async () => {
    render(<Harness selected={reference} onDeselect={vi.fn()} onHide={vi.fn()} />);
    expect(inspectorShown()).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    expect(inspectorShown()).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Reference' }).getAttribute('aria-selected')).toBe('true');
    expect(await screen.findByText('Identity')).toBeTruthy();
  });

  it('switches tabs and shows the edit fields on the Edit tab', async () => {
    render(<Harness selected={reference} onDeselect={vi.fn()} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    fireEvent.click(screen.getByRole('tab', { name: 'Scripts' }));
    expect(await screen.findByText('No scripts on this reference or its base.')).toBeTruthy();
    fireEvent.click(screen.getByRole('tab', { name: 'Edit' }));
    expect(await screen.findByLabelText('Pos X')).toBeTruthy();
    expect(screen.getByRole('tab', { name: 'Edit' }).getAttribute('aria-selected')).toBe('true');
  });

  it('closes with the X button and keeps the selection', () => {
    const onDeselect = vi.fn();
    render(<Harness selected={reference} onDeselect={onDeselect} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    fireEvent.click(screen.getByRole('button', { name: 'Close reference inspector' }));
    expect(inspectorShown()).toBeNull();
    expect(onDeselect).not.toHaveBeenCalled();
    expect(screen.getByRole('button', { name: 'open' })).toBeTruthy();
  });

  it('Escape with focus inside the panel closes it and deselects', () => {
    const onDeselect = vi.fn();
    render(<Harness selected={reference} onDeselect={onDeselect} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    const tab = screen.getByRole('tab', { name: 'Plugins' });
    act(() => { tab.focus(); });
    fireEvent.keyDown(tab, { key: 'Escape' });
    expect(inspectorShown()).toBeNull();
    expect(onDeselect).toHaveBeenCalledTimes(1);
  });

  it('Escape outside the panel closes it without deselecting', () => {
    const onDeselect = vi.fn();
    render(<Harness selected={reference} onDeselect={onDeselect} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    act(() => { screen.getByRole('button', { name: 'open' }).focus(); });
    fireEvent.keyDown(window, { key: 'Escape' });
    expect(inspectorShown()).toBeNull();
    expect(onDeselect).not.toHaveBeenCalled();
  });

  it('Escape right after opening (focus on the panel itself) closes it without deselecting', () => {
    const onDeselect = vi.fn();
    render(<Harness selected={reference} onDeselect={onDeselect} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    expect(document.activeElement).toBe(inspectorShown());
    fireEvent.keyDown(window, { key: 'Escape' });
    expect(inspectorShown()).toBeNull();
    expect(onDeselect).not.toHaveBeenCalled();
  });

  it('the Hide this mesh button reports the hide', () => {
    const onHide = vi.fn();
    render(<Harness selected={reference} onDeselect={vi.fn()} onHide={onHide} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    fireEvent.click(screen.getByRole('button', { name: /Hide this mesh/ }));
    expect(onHide).toHaveBeenCalledTimes(1);
  });
});

describe('ReferenceInspector resize', () => {
  it('dragging the left edge to the left widens the panel, clamped to the maximum', () => {
    const onResize = vi.fn();
    const onCommitWidth = vi.fn();
    render(
      <ReferenceInspector
        reference={reference}
        refRevision={0}
        tab="reference"
        onTab={vi.fn()}
        onClose={vi.fn()}
        onDeselect={vi.fn()}
        onHide={vi.fn()}
        width={420}
        viewportWidth={1600}
        onResize={onResize}
        onCommitWidth={onCommitWidth}
        edit={edit()}
      />,
    );
    const handle = screen.getByRole('separator', { name: 'Resize reference inspector' });
    expect(handle.getAttribute('aria-valuemin')).toBe('320');
    expect(handle.getAttribute('aria-valuemax')).toBe('960');
    fireEvent.mouseDown(handle, { button: 0, clientX: 800 });
    fireEvent.mouseMove(window, { clientX: 700 });
    expect(onResize).toHaveBeenLastCalledWith(520);
    fireEvent.mouseMove(window, { clientX: 0 });
    expect(onResize).toHaveBeenLastCalledWith(960);
    fireEvent.mouseUp(window);
    expect(onCommitWidth).toHaveBeenCalledTimes(1);
    expect(onCommitWidth).toHaveBeenCalledWith(960);
  });

  it('arrow keys step the width and double-click resets to 420px', () => {
    const onResize = vi.fn();
    render(
      <ReferenceInspector
        reference={reference}
        refRevision={0}
        tab="reference"
        onTab={vi.fn()}
        onClose={vi.fn()}
        onDeselect={vi.fn()}
        onHide={vi.fn()}
        width={420}
        viewportWidth={1600}
        onResize={onResize}
        onCommitWidth={vi.fn()}
        edit={edit()}
      />,
    );
    const handle = screen.getByRole('separator', { name: 'Resize reference inspector' });
    fireEvent.keyDown(handle, { key: 'ArrowLeft' });
    expect(onResize).toHaveBeenLastCalledWith(436);
    fireEvent.keyDown(handle, { key: 'ArrowRight' });
    expect(onResize).toHaveBeenLastCalledWith(404);
    fireEvent.doubleClick(handle);
    expect(onResize).toHaveBeenLastCalledWith(420);
  });
});

describe('SelectedSummary', () => {
  it('shows the EditorID and form key on one line and offers Show details', () => {
    const onShow = vi.fn();
    render(<SelectedSummary reference={reference} open={false} onShow={onShow} />);
    expect(screen.getByText('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();
    expect(screen.getByTitle('Selected: Ref1 00000ABC:Plugin.esp')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Show details' }));
    expect(onShow).toHaveBeenCalledTimes(1);
  });

  it('reads Details while the inspector is already open', () => {
    const onShow = vi.fn();
    render(<SelectedSummary reference={reference} open onShow={onShow} />);
    expect(screen.queryByRole('button', { name: 'Show details' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Details' }));
    expect(onShow).toHaveBeenCalledTimes(1);
  });
});

describe('ReferenceInspectorDock focus', () => {
  it('re-opening through the inspector focuses the panel and scrolls its body to the top', async () => {
    render(<Harness selected={reference} onDeselect={vi.fn()} onHide={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    const panel = inspectorShown() as HTMLElement;
    await screen.findByText('Identity');
    const body = panel.querySelector('.ref-inspector-body') as HTMLElement;
    body.scrollTop = 120;
    act(() => { (document.activeElement as HTMLElement | null)?.blur(); });
    fireEvent.click(screen.getByRole('button', { name: 'open' }));
    expect(document.activeElement).toBe(panel);
    expect(body.scrollTop).toBe(0);
  });
});
