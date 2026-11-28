import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import SwfPanel from './SwfPanel';

afterEach(cleanup);

vi.mock('./useDialogs', () => ({ useDialogs: () => ({ confirm: vi.fn(async () => true) }) }));

function stubArchiveBridge() {
  const swf = {
    ArchiveSwfs: vi.fn(async () => JSON.stringify({ entries: [{ path: 'interface/test.swf', archive: 'Fallout4 - Interface.ba2', size: 10 }] })),
    OpenArchived: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false, archived: true })),
    SaveAs: vi.fn(async () => JSON.stringify({ saved: 'D:\\Out\\test.swf' })),
    Save: vi.fn(async () => JSON.stringify({ saved: 'interface/test.swf' })),
    Open: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    State: vi.fn(async () => JSON.stringify({ revision: 0, undo: 0, redo: 0, dirty: false })),
    Characters: vi.fn(async () => JSON.stringify({ characters: [] })),
    Timeline: vi.fn(async () => JSON.stringify({ characterId: 0, frameCount: 1 })),
    FrameLabels: vi.fn(async () => JSON.stringify({ labels: [] })),
    Frame: vi.fn(async () => JSON.stringify({ rows: [] })),
    Placements: vi.fn(async () => JSON.stringify({ placements: [] })),
    AbcBlocks: vi.fn(async () => JSON.stringify({ blocks: [] })),
    InspectText: vi.fn(async () => JSON.stringify({ text: '' })),
    ActionTags: vi.fn(async () => JSON.stringify({ tags: [] })),
    FrameSvg: vi.fn(async () => JSON.stringify({ svg: '' })),
  };
  vi.stubGlobal('chrome', { webview: { hostObjects: { swf } } });
  return swf;
}

async function openArchiveEntry() {
  render(<SwfPanel onClose={() => {}} />);
  await act(async () => { fireEvent.click(screen.getByText('From game archives…')); });
  fireEvent.change(await screen.findByLabelText('Game archive SWFs'), { target: { value: 'interface/test.swf' } });
}

describe('SwfPanel game archives', () => {
  // Detects: a game archive SWF missing from the list, or the picked entry opened by its path instead of its resolved name.
  it('lists the game archive SWFs and opens the picked one by its resolved name', async () => {
    const swf = stubArchiveBridge();
    await openArchiveEntry();
    await waitFor(() => expect(swf.OpenArchived).toHaveBeenCalledWith('interface/test.swf'));
    expect(swf.Open).not.toHaveBeenCalled();
    expect(screen.queryByLabelText('Game archive SWFs')).toBeNull();
  });

  // Detects: an archive-opened movie written in place through Save, or a Save As that leaves the Save button in archive mode.
  it('saves an archive-opened movie only through Save As', async () => {
    const swf = stubArchiveBridge();
    await openArchiveEntry();
    const saveAs = await screen.findByTitle('Save As');
    await act(async () => { fireEvent.click(saveAs); });
    await waitFor(() => expect(swf.SaveAs).toHaveBeenCalledWith('Save the SWF movie', 'SWF movie (*.swf)|*.swf|All files|*.*'));
    expect(swf.Save).not.toHaveBeenCalled();
    expect(await screen.findByTitle('Save')).toBeTruthy();
  });
});
