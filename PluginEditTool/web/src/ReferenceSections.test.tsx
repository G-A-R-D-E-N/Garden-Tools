import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { ReferenceTab, type ReferenceEditProps } from './ReferencePanel';
import type { ReferenceDetails } from './referenceDetails';
import type { ReferenceData } from './useReferenceData';

vi.mock('./backend', () => ({ getBackend: () => ({}), getCell: () => ({}) }));

const details = {
  formKey: '00000ABC:Plugin.esp',
  editorId: 'Ref1',
  signature: 'REFR',
  flags: {
    raw: 0x400,
    deleted: false,
    persistent: true,
    initiallyDisabled: false,
    respawns: true,
    names: [{ name: 'Persistent', bit: 10 }],
  },
  openByDefault: false,
  ignoredBySandbox: false,
} as unknown as ReferenceDetails;

const edit = {
  patchChosen: true,
  canUndo: false,
  canRedo: false,
  onEdit: vi.fn(async () => true),
  onDelete: vi.fn(async () => {}),
  onDuplicate: vi.fn(async () => {}),
  onUndoEdit: vi.fn(async () => {}),
  onRedoEdit: vi.fn(async () => {}),
} as unknown as ReferenceEditProps;

function dataFor(loading: boolean): ReferenceData {
  return loading
    ? { loading: true, error: null, details: null, plugins: [] }
    : { loading: false, error: null, details, plugins: [] };
}

afterEach(() => cleanup());

describe('reference inspector sections', () => {
  it('keeps a section the user opened when the same reference reloads', () => {
    const { rerender } = render(<ReferenceTab data={dataFor(false)} edit={edit} />);
    const flags = screen.getByText('Flags').closest('details') as HTMLDetailsElement;
    expect(flags.open).toBe(false);

    flags.open = true;
    fireEvent(flags, new Event('toggle'));
    rerender(<ReferenceTab data={dataFor(true)} edit={edit} />);
    expect(screen.queryByText('Flags')).toBeNull();

    rerender(<ReferenceTab data={dataFor(false)} edit={edit} />);
    expect((screen.getByText('Flags').closest('details') as HTMLDetailsElement).open).toBe(true);
  });

  it('keeps a section the user closed when it was open by default', () => {
    const { rerender } = render(<ReferenceTab data={dataFor(false)} edit={edit} />);
    const identity = screen.getByText('Identity').closest('details') as HTMLDetailsElement;
    expect(identity.open).toBe(true);

    identity.open = false;
    fireEvent(identity, new Event('toggle'));
    rerender(<ReferenceTab data={dataFor(true)} edit={edit} />);
    rerender(<ReferenceTab data={dataFor(false)} edit={edit} />);
    expect((screen.getByText('Identity').closest('details') as HTMLDetailsElement).open).toBe(false);
  });
});
