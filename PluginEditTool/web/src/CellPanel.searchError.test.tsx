import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import CellPanel from './CellPanel';

const cell = vi.hoisted(() => ({
  GetPlugins: async () => '["Fallout4.esm"]',
  SearchCells: async () => { throw new Error('cell index offline'); },
  SearchWorldspaces: async () => '[]',
}));

vi.mock('./backend', () => ({ getCell: () => cell, getBackend: () => ({}) }));
vi.mock('./useDialogs', () => ({ useDialogs: () => ({ pickPlugin: vi.fn() }) }));
vi.mock('./CellViewport', () => ({ default: () => null }));

afterEach(() => { cleanup(); localStorage.clear(); });

it('shows a failed cell search instead of an empty list', async () => {
  render(<CellPanel onClose={() => {}} />);
  expect(await screen.findByText('Could not search cells: cell index offline')).toBeTruthy();
});
