import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import RecordPicker from './RecordPicker';

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('shows a failed search instead of reporting no matches', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    SearchRecords: vi.fn(async () => { throw new Error('index offline'); }),
  } } } });
  render(<RecordPicker title="Race" refType={null} refTypes={null} onPick={() => {}} onClose={() => {}} />);
  expect(await screen.findByText('Search failed: index offline')).toBeTruthy();
});
