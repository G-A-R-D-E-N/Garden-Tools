import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import DestructiblePanel from './DestructiblePanel';

afterEach(cleanup);

const present = {
  status: 'Present',
  error: null,
  header: { health: 750, stageCount: 2, flags: 3 },
  resistances: [{ damageType: '000A10:Fallout4.esm', value: 5 }],
  stages: [
    {
      members: [{ signature: 'DSTD', size: 20, hex: '00'.repeat(20) }, { signature: 'DSTA', size: 1, hex: '00' }],
      values: { healthPercent: '10', index: '0', modelDamageStage: '0', flags: '1', selfDamage: '1', debrisCount: '0' },
    },
    {
      members: [{ signature: 'DSTD', size: 20, hex: '00'.repeat(20) }, { signature: 'DMDL', size: 1, hex: '00' }],
      values: { healthPercent: '50', index: '1', modelDamageStage: '0', flags: '0', selfDamage: '2', debrisCount: '0' },
    },
  ],
};

function reply(value: Record<string, unknown>) {
  return JSON.stringify({ path: 'Data/Test.esp', plugin: 'Test.esp', dirty: false, error: null, ...value });
}

function stubBridge(overrides: Record<string, unknown> = {}) {
  const destructible = {
    OpenGroupEditor: vi.fn(async () => reply({})),
    ReadGroup: vi.fn(async () => reply({ group: present })),
    InsertGroupHeader: vi.fn(async () => reply({ dirty: true, group: present })),
    RemoveGroupHeader: vi.fn(async () => reply({ dirty: true, group: { ...present, stages: [], header: null, status: 'HeaderMissing', error: 'no header' } })),
    InsertGroupStage: vi.fn(async () => reply({ dirty: true, group: present })),
    RemoveGroupStage: vi.fn(async () => reply({ dirty: true, group: { ...present, stages: [present.stages[1]] } })),
    InsertGroupStageMember: vi.fn(async () => reply({ dirty: true, group: present })),
    RemoveGroupStageMember: vi.fn(async () => reply({ dirty: true, group: present })),
    SetGroupHeader: vi.fn(async () => reply({ dirty: true, group: present })),
    SetGroupStageValue: vi.fn(async () => reply({ dirty: true, group: present })),
    SetGroupResistanceValue: vi.fn(async () => reply({ dirty: true, group: present })),
    SetGroupResistanceDamageType: vi.fn(async () => reply({ dirty: true, group: present })),
    CreateGroupRecord: vi.fn(async () => reply({ formId: '000A00', dirty: true, group: { ...present, stages: [] } })),
    SaveGroup: vi.fn(async () => JSON.stringify({ saved: 'Data/Test.esp', reloaded: true, plugin: 'Test.esp', dirty: false })),
    ...overrides,
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { destructible } } });
  return destructible;
}

async function open() {
  render(<DestructiblePanel onClose={() => {}} />);
  fireEvent.change(screen.getByPlaceholderText('Data/Fallout4.esm or a plugin path'), { target: { value: 'Data/Test.esp' } });
  await act(async () => { fireEvent.click(screen.getByText('Open')); });
  fireEvent.change(screen.getByPlaceholderText('000800'), { target: { value: '000800' } });
  await act(async () => { fireEvent.click(screen.getByText('Load')); });
}

describe('DestructiblePanel', () => {
  it('renders the group as stages with their member rows', async () => {
    const destructible = stubBridge();
    await open();

    await waitFor(() => expect(destructible.ReadGroup).toHaveBeenCalledWith('000800'));
    expect(await screen.findByText(/Stage 1 · 2 members/)).toBeTruthy();
    expect(screen.getByText(/Stage 2 · 2 members/)).toBeTruthy();
    expect(screen.getAllByText('DSTA', { selector: '.dest-sig' })).toHaveLength(1);
    expect(screen.getByText(/health 750 · stages 2 · flags 3/)).toBeTruthy();
  });

  it('shows a refusal with no stage rows when the header is missing', async () => {
    stubBridge({
      ReadGroup: vi.fn(async () => reply({
        group: { status: 'HeaderMissing', error: 'The record has no DEST header, so it has no destructible group.',
          header: null, resistances: null, stages: null },
      })),
    });
    await open();

    expect(await screen.findByText('HeaderMissing')).toBeTruthy();
    expect(screen.getByText(/has no DEST header/)).toBeTruthy();
    // Constraint: detects a refusal rendered as an empty stage list, which the author reads as "no stages".
    expect(screen.queryByText(/^Stage 1 /)).toBeNull();
    expect(screen.queryByText('Add stage')).toBeNull();
  });

  it('sends stage and member edits and saves through the group RPCs', async () => {
    const destructible = stubBridge();
    await open();

    await act(async () => { fireEvent.click(await screen.findByText('Add stage')); });
    expect(destructible.InsertGroupStage).toHaveBeenCalledWith('000800', 2);

    await act(async () => { fireEvent.click(screen.getAllByText('Add DSTA')[0]); });
    expect(destructible.InsertGroupStageMember).toHaveBeenCalledWith('000800', 0, 'DSTA');

    await act(async () => { fireEvent.click(screen.getAllByTitle('Remove stage')[0]); });
    expect(destructible.RemoveGroupStage).toHaveBeenCalledWith('000800', 0);
    await waitFor(() => expect(screen.queryByText(/Stage 2 · 2 members/)).toBeNull());

    await act(async () => { fireEvent.click(screen.getByText('Save')); });
    expect(destructible.SaveGroup).toHaveBeenCalled();
    await waitFor(() => expect(screen.queryByText(/unsaved/)).toBeNull());
  });

  // Constraint: detects a header editor that writes the shown values instead of the edited ones, which silently
  // leaves the record's health at the value it was loaded with, so the write must carry the edited value.
  it('writes the edited header health and flags', async () => {
    const destructible = stubBridge();
    await open();

    fireEvent.change(await screen.findByLabelText('health'), { target: { value: '900' } });
    fireEvent.change(screen.getByLabelText('flags'), { target: { value: '2' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply header')); });

    expect(destructible.SetGroupHeader).toHaveBeenCalledWith('000800', 900, 2);
  });

  // Constraint: detects a stage editor that writes at the wrong stage ordinal, which would change the neighbouring
  // stage's DSTD row instead of the one the author is looking at, so the write must target its own ordinal.
  it('writes one changed stage value at its own stage index', async () => {
    const destructible = stubBridge();
    await open();

    const percent = await screen.findAllByLabelText('stage 2 Health %');
    fireEvent.change(percent[0], { target: { value: '75' } });
    await act(async () => { fireEvent.click(screen.getAllByText('Apply values')[1]); });

    expect(destructible.SetGroupStageValue).toHaveBeenCalledWith('000800', 1, 'healthPercent', '75');
    expect(destructible.SetGroupStageValue).toHaveBeenCalledTimes(1);
  });

  it('writes an edited DAMC resistance value', async () => {
    const destructible = stubBridge();
    await open();

    fireEvent.change(await screen.findByLabelText('resistance 1'), { target: { value: '9' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply resistance')); });

    expect(destructible.SetGroupResistanceValue).toHaveBeenCalledWith('000800', 0, '9');
    expect(destructible.SetGroupResistanceDamageType).not.toHaveBeenCalled();
  });

  // Constraint: detects a resistance editor that never writes the link, or writes it at the wrong DAMC index, which
  // leaves the author's damage type on a neighbouring entry and the value on this one, so the index must match.
  it('writes an edited resistance damage-type link at its own index', async () => {
    const destructible = stubBridge();
    await open();

    const link = await screen.findByLabelText('resistance 1 damage type');
    expect((link as HTMLInputElement).value).toBe('000A10:Fallout4.esm');
    fireEvent.change(link, { target: { value: '000A11:Fallout4.esm' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply resistance')); });

    expect(destructible.SetGroupResistanceDamageType).toHaveBeenCalledWith('000800', 0, '000A11:Fallout4.esm');
    expect(destructible.SetGroupResistanceValue).not.toHaveBeenCalled();
  });

  // Constraint: detects a create path that reports success without loading the new record, which leaves the panel
  // editing whichever record it had open before, so the panel must switch to the created record.
  it('creates a carrier record with a DEST group and switches to it', async () => {
    const destructible = stubBridge();
    await open();

    fireEvent.change(screen.getByLabelText('record signature'), { target: { value: 'ACTI' } });
    fireEvent.change(screen.getByLabelText('editor ID'), { target: { value: 'FreshDestructible' } });
    await act(async () => { fireEvent.click(screen.getByText('Create record')); });

    expect(destructible.CreateGroupRecord).toHaveBeenCalledWith('ACTI', 'FreshDestructible');
    const input = screen.getByPlaceholderText('000800') as HTMLInputElement;
    await waitFor(() => expect(input.value).toBe('000A00'));
  });

  async function makeDirty() {
    await act(async () => { fireEvent.click(screen.getByText('Insert header')); });
    await waitFor(() => expect(screen.getByText(/unsaved/)).toBeTruthy());
  }

  const saveButton = () => screen.getByText('Save').closest('button') as HTMLButtonElement;

  // Constraint: detects a save that replaces the group with the reply's absent group, which blanks every stage row right after the author saved.
  it('keeps the group on screen when a save reply carries no group', async () => {
    const destructible = stubBridge();
    await open();
    await screen.findByText(/Stage 1 · 2 members/);
    await makeDirty();

    await act(async () => { fireEvent.click(saveButton()); });

    expect(destructible.SaveGroup).toHaveBeenCalled();
    await waitFor(() => expect(screen.queryByText(/unsaved/)).toBeNull());
    expect(screen.getByText(/Stage 1 · 2 members/)).toBeTruthy();
    expect(screen.getByText(/Stage 2 · 2 members/)).toBeTruthy();
    expect(screen.getByText('Test.esp')).toBeTruthy();
  });

  // Constraint: detects an error reply that clears the group and the dirty flag, which leaves unsaved native edits with a disabled Save button.
  it('shows a refused edit without clearing the group or disabling Save', async () => {
    stubBridge({ InsertGroupStage: vi.fn(async () => JSON.stringify({ error: 'the stage was refused' })) });
    await open();
    await screen.findByText(/Stage 1 · 2 members/);
    await makeDirty();

    await act(async () => { fireEvent.click(screen.getByText('Add stage')); });

    expect(await screen.findByText(/the stage was refused/)).toBeTruthy();
    expect(screen.getByText(/Stage 1 · 2 members/)).toBeTruthy();
    expect(screen.getByText(/unsaved/)).toBeTruthy();
    expect(saveButton().disabled).toBe(false);
  });

  // Constraint: detects a multi-field apply that keeps sending after a failure, which lets a later success overwrite the earlier error.
  it('stops a stage apply at the first failed write and keeps its error', async () => {
    const destructible = stubBridge({
      SetGroupStageValue: vi.fn()
        .mockResolvedValueOnce(JSON.stringify({ error: 'health percent refused' }))
        .mockResolvedValue(reply({ dirty: true, group: present })),
    });
    await open();
    await screen.findByText(/Stage 1 · 2 members/);

    fireEvent.change(screen.getAllByLabelText('stage 1 Health %')[0], { target: { value: '99' } });
    fireEvent.change(screen.getAllByLabelText('stage 1 Flags')[0], { target: { value: '3' } });
    await act(async () => { fireEvent.click(screen.getAllByText('Apply values')[0]); });

    expect(destructible.SetGroupStageValue).toHaveBeenCalledTimes(1);
    expect(await screen.findByText(/health percent refused/)).toBeTruthy();
    expect(screen.getByText(/Stage 1 · 2 members/)).toBeTruthy();
  });

  it('shows the error of a later failed stage write and keeps the group', async () => {
    const destructible = stubBridge({
      SetGroupStageValue: vi.fn()
        .mockResolvedValueOnce(reply({ dirty: true, group: present }))
        .mockResolvedValue(JSON.stringify({ error: 'flags refused' })),
    });
    await open();
    await screen.findByText(/Stage 1 · 2 members/);

    fireEvent.change(screen.getAllByLabelText('stage 1 Health %')[0], { target: { value: '99' } });
    fireEvent.change(screen.getAllByLabelText('stage 1 Flags')[0], { target: { value: '3' } });
    await act(async () => { fireEvent.click(screen.getAllByText('Apply values')[0]); });

    expect(destructible.SetGroupStageValue).toHaveBeenCalledTimes(2);
    expect(await screen.findByText(/flags refused/)).toBeTruthy();
    expect(screen.getByText(/Stage 1 · 2 members/)).toBeTruthy();
    expect(screen.getByText(/unsaved/)).toBeTruthy();
  });

  it('does not write the resistance value after its damage-type link was refused', async () => {
    const destructible = stubBridge({
      SetGroupResistanceDamageType: vi.fn(async () => JSON.stringify({ error: 'damage type refused' })),
    });
    await open();

    fireEvent.change(await screen.findByLabelText('resistance 1 damage type'), { target: { value: '000A11:Fallout4.esm' } });
    fireEvent.change(screen.getByLabelText('resistance 1'), { target: { value: '9' } });
    await act(async () => { fireEvent.click(screen.getByText('Apply resistance')); });

    expect(await screen.findByText(/damage type refused/)).toBeTruthy();
    expect(destructible.SetGroupResistanceValue).not.toHaveBeenCalled();
    expect(screen.getByText(/Stage 1 · 2 members/)).toBeTruthy();
  });
});
