import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { CellEditChanges } from './backend';
import type { ReferenceDetails } from './referenceDetails';
import ReferenceOwnershipEditor from './ReferenceOwnershipEditor';

vi.mock('./RecordPicker', async () => {
  const { createElement } = await import('react');
  return {
    default: (props: { title: string; onPick: (formKey: string, plugin: string) => void }) =>
      createElement('button', { onClick: () => props.onPick(props.title === 'Owner faction' ? '000840:Base.esm' : '000830:Base.esm', 'Base.esm') }, `Stub ${props.title}`),
  };
});

afterEach(() => cleanup());

type Ownership = ReferenceDetails['ownership'];

const npcOwned: Ownership = { owner: { formKey: '000830:Base.esm', editorId: 'Npc', signature: 'NPC_' }, noCrime: false };
const factionOwned: Ownership = { owner: { formKey: '000840:Base.esm', editorId: 'Fac', signature: 'FACT' }, noCrime: true };

function editWith(patchChosen = true, result = true) {
  return {
    patchChosen,
    onEdit: vi.fn<(changes: CellEditChanges) => Promise<boolean>>(async () => result),
  };
}

describe('ReferenceOwnershipEditor', () => {
  it('disables the rank input for an NPC owner and applies an owner change without rank', () => {
    const edit = editWith();
    render(<ReferenceOwnershipEditor ownership={npcOwned} factionRank={null} edit={edit} />);
    expect((screen.getByLabelText('Faction rank') as HTMLInputElement).disabled).toBe(true);
    fireEvent.click(screen.getByLabelText('No crime'));
    fireEvent.click(screen.getByRole('button', { name: 'Apply owner' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ ownership: { owner: '000830:Base.esm', noCrime: true, rank: null } });
  });

  it('enables rank for a faction owner and sends the rank', () => {
    const edit = editWith();
    render(<ReferenceOwnershipEditor ownership={factionOwned} factionRank={2} edit={edit} />);
    const rank = screen.getByLabelText('Faction rank') as HTMLInputElement;
    expect(rank.disabled).toBe(false);
    fireEvent.change(rank, { target: { value: '4' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply owner' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ ownership: { owner: '000840:Base.esm', noCrime: true, rank: 4 } });
  });

  it('refuses a non-whole rank and keeps Apply disabled', () => {
    const edit = editWith();
    render(<ReferenceOwnershipEditor ownership={factionOwned} factionRank={2} edit={edit} />);
    fireEvent.change(screen.getByLabelText('Faction rank'), { target: { value: '1.5' } });
    expect(screen.getByText('Faction rank must be a whole number.')).toBeTruthy();
    expect((screen.getByRole('button', { name: 'Apply owner' }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('picks an NPC owner or a faction owner and clears the owner through null', () => {
    const edit = editWith();
    render(<ReferenceOwnershipEditor ownership={npcOwned} factionRank={null} edit={edit} />);
    fireEvent.click(screen.getByRole('button', { name: 'Pick faction' }));
    fireEvent.click(screen.getByRole('button', { name: 'Stub Owner faction' }));
    expect(screen.getByText('000840:Base.esm')).toBeTruthy();
    expect((screen.getByLabelText('Faction rank') as HTMLInputElement).disabled).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: 'Clear owner' }));
    fireEvent.click(screen.getByRole('button', { name: 'Apply owner' }));
    expect(edit.onEdit).toHaveBeenCalledWith({ ownership: null });
  });
});
