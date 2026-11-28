import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import PluginCompareDialog, { type CompareRow, type RecordSides } from './PluginCompareDialog';

afterEach(cleanup);

const row = (FormKey: string, extra: Partial<CompareRow> = {}): CompareRow => ({
  FormKey, Signature: 'MISC', Change: 'changed', HeaderChanged: false, PlacementChanged: false, Subrecords: [], ...extra,
});

describe('PluginCompareDialog', () => {
  it('shows both versions of the picked record and marks differing subrecords', async () => {
    const sides: RecordSides = {
      FormKey: '000810:Mod.esp',
      Before: [{ Signature: 'EDID', Size: 6, Hex: '', Text: 'Older' }, { Signature: 'DATA', Size: 4, Hex: '2A 00 00 00', Text: '' }],
      After: [{ Signature: 'EDID', Size: 8, Hex: '', Text: 'Current' }, { Signature: 'DATA', Size: 4, Hex: '2A 00 00 00', Text: '' }],
      Differs: [true, false],
    };
    const loadRecord = vi.fn(async () => sides);
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'C:\\Backups\\Mod.esp', loadRecord,
      rows: [row('000810:Mod.esp', { Subrecords: ['EDID'] })],
    }} />);

    fireEvent.click(screen.getByText(/000810:Mod\.esp/));
    expect(loadRecord).toHaveBeenCalledWith('000810:Mod.esp');
    const older = await screen.findByText('"Older"');
    expect(older.closest('tr')?.className).toBe('pcd-diff');
    expect(screen.getByText('"Current"').closest('tr')).toBe(older.closest('tr'));
    expect(screen.getAllByText('2A 00 00 00')[0].closest('tr')?.className).toBe('');
  });

  it('marks a subrecord that differs only after the 64-byte preview', async () => {
    const preview = Array.from({ length: 64 }, () => '00').join(' ') + ' ...';
    const data = { Signature: 'DATA', Size: 80, Hex: preview, Text: '' };
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'Old.esp', rows: [row('000812:Mod.esp', { Subrecords: ['DATA'] })],
      loadRecord: async () => ({ FormKey: '000812:Mod.esp', Before: [data], After: [{ ...data }], Differs: [true] }),
    }} />);
    fireEvent.click(screen.getByText(/000812:Mod\.esp/));
    const cells = await screen.findAllByText(preview);
    expect(cells[0].closest('tr')?.className).toBe('pcd-diff');
  });

  it('keeps the latest picked record when an earlier load finishes last', async () => {
    let finishA: (value: RecordSides) => void = () => {};
    let finishB: (value: RecordSides) => void = () => {};
    const loadRecord = vi.fn((formKey: string) => new Promise<RecordSides>(resolve => {
      if (formKey.startsWith('00081A')) finishA = resolve; else finishB = resolve;
    }));
    const sides = (formKey: string, text: string): RecordSides => ({
      FormKey: formKey, Before: [{ Signature: 'EDID', Size: 4, Hex: '', Text: text }], After: null, Differs: [true],
    });
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'Old.esp', loadRecord, rows: [row('00081A:Mod.esp'), row('00081B:Mod.esp')],
    }} />);

    fireEvent.click(screen.getByText(/00081A:Mod\.esp/));
    fireEvent.click(screen.getByText(/00081B:Mod\.esp/));
    await act(async () => finishB(sides('00081B:Mod.esp', 'RecordB')));
    await act(async () => finishA(sides('00081A:Mod.esp', 'RecordA')));

    expect(screen.getByText('"RecordB"')).toBeTruthy();
    expect(screen.queryByText('"RecordA"')).toBeNull();
  });

  it('says why a record with identical subrecords counts as changed', async () => {
    const same: RecordSides = {
      FormKey: '', Before: [{ Signature: 'EDID', Size: 4, Hex: '', Text: 'Ref' }],
      After: [{ Signature: 'EDID', Size: 4, Hex: '', Text: 'Ref' }], Differs: [false],
    };
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'Old.esp', loadRecord: async formKey => ({ ...same, FormKey: formKey }),
      rows: [row('000813:Mod.esp', { HeaderChanged: true }), row('000814:Mod.esp', { Signature: 'REFR', PlacementChanged: true })],
    }} />);

    fireEvent.click(screen.getByText(/000813:Mod\.esp/));
    expect(await screen.findByText('Record header flags differ.')).toBeTruthy();
    expect(screen.queryByText('Record is in a different parent group.')).toBeNull();

    fireEvent.click(screen.getByText(/000814:Mod\.esp/));
    expect(await screen.findByText('Record is in a different parent group.')).toBeTruthy();
    expect(screen.queryByText('Record header flags differ.')).toBeNull();
  });

  it('says when a record exists in only one file', async () => {
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'Old.esp',
      loadRecord: async () => ({ FormKey: '000811:Mod.esp', Before: null, After: [{ Signature: 'EDID', Size: 4, Hex: '', Text: 'New' }], Differs: [true] }),
      rows: [row('000811:Mod.esp', { Change: 'added' })],
    }} />);
    fireEvent.click(screen.getByText(/000811:Mod\.esp/));
    expect(await screen.findByText('(not in this file)')).toBeTruthy();
  });
});
