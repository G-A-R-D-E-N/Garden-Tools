import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import PluginCompareDialog, { type RecordSides } from './PluginCompareDialog';

afterEach(cleanup);

describe('PluginCompareDialog', () => {
  it('shows both versions of the picked record and marks differing subrecords', async () => {
    const sides: RecordSides = {
      FormKey: '000810:Mod.esp',
      Before: [{ Signature: 'EDID', Size: 6, Hex: '', Text: 'Older' }, { Signature: 'DATA', Size: 4, Hex: '2A 00 00 00', Text: '' }],
      After: [{ Signature: 'EDID', Size: 8, Hex: '', Text: 'Current' }, { Signature: 'DATA', Size: 4, Hex: '2A 00 00 00', Text: '' }],
    };
    const loadRecord = vi.fn(async () => sides);
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'C:\\Backups\\Mod.esp', loadRecord,
      rows: [{ FormKey: '000810:Mod.esp', Signature: 'MISC', Change: 'changed', Subrecords: ['EDID'] }],
    }} />);

    fireEvent.click(screen.getByText(/000810:Mod\.esp/));
    expect(loadRecord).toHaveBeenCalledWith('000810:Mod.esp');
    const older = await screen.findByText('"Older"');
    expect(older.closest('tr')?.className).toBe('pcd-diff');
    expect(screen.getByText('"Current"').closest('tr')).toBe(older.closest('tr'));
    expect(screen.getAllByText('2A 00 00 00')[0].closest('tr')?.className).toBe('');
  });

  it('says when a record exists in only one file', async () => {
    render(<PluginCompareDialog onClose={() => {}} req={{
      plugin: 'Mod.esp', otherPath: 'Old.esp',
      loadRecord: async () => ({ FormKey: '000811:Mod.esp', Before: null, After: [{ Signature: 'EDID', Size: 4, Hex: '', Text: 'New' }] }),
      rows: [{ FormKey: '000811:Mod.esp', Signature: 'MISC', Change: 'added', Subrecords: [] }],
    }} />);
    fireEvent.click(screen.getByText(/000811:Mod\.esp/));
    expect(await screen.findByText('(not in this file)')).toBeTruthy();
  });
});
