import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { ConflictMatrix } from '../backend';
import DetailRail from './DetailRail';

const matrix: ConflictMatrix = {
  FormKey: '000800:W0.esp', EditorID: 'TestRace', Type: 'RACE', Winner: 'W0.esp',
  Plugins: ['W0.esp'], Level: 'onlyone', Rows: [],
};

afterEach(() => { cleanup(); vi.unstubAllGlobals(); });

it('shows a failed details read instead of an empty rail', async () => {
  vi.stubGlobal('chrome', { webview: { hostObjects: { backend: {
    GetRecordDetails: vi.fn(async () => { throw new Error('details offline'); }),
    GetRecordPluginMatrix: vi.fn(async () => '[]'),
    GetContainmentPath: vi.fn(async () => '[]'),
  } } } });
  render(<DetailRail matrix={matrix} />);
  expect(await screen.findByText('Could not load record details: details offline')).toBeTruthy();
});
