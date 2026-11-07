import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import RecordPicker from './RecordPicker';

const hit = (EditorID: string, Type: string, FormKey: string, Plugin = 'Fallout4.esm') =>
  ({ FormKey, EditorID, Type, Plugin });

const ids = () => Array.from(document.querySelectorAll('.rp-id')).map(el => el.textContent);

function renderPicker(refTypes: string, SearchRecords: ReturnType<typeof vi.fn>) {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: { SearchRecords } } } });
  return render(
    <RecordPicker title="Test" refType={refTypes} refTypes={refTypes} onPick={() => {}} onClose={() => {}} />,
  );
}

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

describe('RecordPicker multi-type search', () => {
  it('drops a record returned by two types and sorts the merged rows by EditorID then type', async () => {
    const same = hit('Dup', 'FACT', '000D01:Fallout4.esm');
    const SearchRecords = vi.fn(async (_q: string, type: string) =>
      JSON.stringify(type === 'NPC_' ? [hit('Zed', 'NPC_', '000A01:Fallout4.esm'), same]
        : [same, hit('Alpha', 'FACT', '000B01:Fallout4.esm')]));
    renderPicker('NPC_,FACT', SearchRecords);
    await waitFor(() => expect(ids()).toEqual(['Alpha', 'Dup', 'Zed']));
  });

  it('shows the rows from the types that succeeded when one type fails', async () => {
    const SearchRecords = vi.fn(async (_q: string, type: string) => {
      if (type === 'NPC_') throw new Error('npc index offline');
      return JSON.stringify([hit('Alpha', 'FACT', '000B01:Fallout4.esm')]);
    });
    renderPicker('NPC_,FACT', SearchRecords);
    await waitFor(() => expect(ids()).toEqual(['Alpha']));
    expect(screen.queryByText(/Search failed/)).toBeNull();
  });

  it('reports the failure only when every type fails', async () => {
    const SearchRecords = vi.fn(async () => { throw new Error('index offline'); });
    renderPicker('NPC_,FACT', SearchRecords);
    expect(await screen.findByText('Search failed: index offline')).toBeTruthy();
  });

  it('a slow response for an older query cannot overwrite the newer results', async () => {
    let resolveOld: (value: string) => void = () => {};
    const SearchRecords = vi.fn((query: string) => query === ''
      ? new Promise<string>(resolve => { resolveOld = resolve; })
      : Promise.resolve(JSON.stringify([hit('Fresh', 'FACT', '000E01:Fallout4.esm')])));
    renderPicker('FACT', SearchRecords);
    await waitFor(() => expect(SearchRecords).toHaveBeenCalledWith('', 'FACT'));
    fireEvent.change(screen.getByPlaceholderText(/Search EditorID/), { target: { value: 'fr' } });
    expect(await screen.findByText('Fresh')).toBeTruthy();
    await act(async () => { resolveOld(JSON.stringify([hit('Stale', 'FACT', '000E02:Fallout4.esm')])); });
    expect(screen.queryByText('Stale')).toBeNull();
    expect(ids()).toEqual(['Fresh']);
  });
});
