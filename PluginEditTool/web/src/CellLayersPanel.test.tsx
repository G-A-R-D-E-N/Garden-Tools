import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import CellLayersPanel from './CellLayersPanel';
import { DEFAULT_LIGHTING_SETTINGS, type LightingSettings } from './cellLighting';

const groups: [string, number][] = [['(markers)', 4], ['STAT', 2]];

function renderPanel(overrides: Partial<Parameters<typeof CellLayersPanel>[0]> = {}) {
  const props = {
    typeGroups: groups,
    hiddenTypes: new Set<string>(),
    onToggleType: vi.fn(),
    onShowAllTypes: vi.fn(),
    settings: DEFAULT_LIGHTING_SETTINGS,
    onSettingsChange: vi.fn(),
    ...overrides,
  };
  render(<CellLayersPanel {...props} />);
  return props;
}

beforeEach(() => { localStorage.clear(); });
afterEach(() => { cleanup(); localStorage.clear(); });

describe('CellLayersPanel', () => {
  it('lists each record type with its count and toggles visibility on click', () => {
    const props = renderPanel();
    expect(screen.getByText('Layers (2)')).toBeTruthy();
    expect(screen.getByText('(markers)')).toBeTruthy();
    expect(screen.getByText('4')).toBeTruthy();
    fireEvent.click(screen.getByText('STAT'));
    expect(props.onToggleType).toHaveBeenCalledWith('STAT');
  });

  it('shows the empty hint instead of rows before a cell is loaded', () => {
    renderPanel({ typeGroups: [] });
    expect(screen.getByText('Layers (0)')).toBeTruthy();
    expect(screen.getByText('Load a cell to list its record types.')).toBeTruthy();
  });

  it('offers Show all only when a record type is hidden', () => {
    const { onShowAllTypes } = renderPanel({ hiddenTypes: new Set(['STAT']) });
    fireEvent.click(screen.getByRole('button', { name: 'Show all' }));
    expect(onShowAllTypes).toHaveBeenCalledTimes(1);
  });

  it('has no Show all action when nothing is hidden', () => {
    renderPanel();
    expect(screen.queryByRole('button', { name: 'Show all' })).toBeNull();
  });

  it('category checkboxes write the layer flag into the settings', () => {
    const onSettingsChange = vi.fn<(next: LightingSettings) => void>();
    renderPanel({ onSettingsChange });
    fireEvent.click(screen.getByRole('checkbox', { name: 'Triggers' }));
    const next = onSettingsChange.mock.calls[0][0] as LightingSettings;
    expect(next.layers.trigger).toBe(false);
    expect(next.layers.room).toBe(true);
  });

  it('lighting controls write their flags, gain, exposure and neutral state', () => {
    const onSettingsChange = vi.fn<(next: LightingSettings) => void>();
    renderPanel({ onSettingsChange });
    fireEvent.click(screen.getByRole('checkbox', { name: 'Cell fog' }));
    expect((onSettingsChange.mock.calls[0][0] as LightingSettings).fog).toBe(!DEFAULT_LIGHTING_SETTINGS.fog);

    fireEvent.change(screen.getByRole('slider', { name: /Light gain/ }), { target: { value: '2' } });
    expect((onSettingsChange.mock.calls[1][0] as LightingSettings).gain).toBe(2);

    fireEvent.click(screen.getByRole('button', { name: 'Neutral lighting' }));
    expect((onSettingsChange.mock.calls[2][0] as LightingSettings).neutral).toBe(true);
  });

  it('reflects the stored category state on the checkboxes', () => {
    renderPanel({ settings: { ...DEFAULT_LIGHTING_SETTINGS, layers: { ...DEFAULT_LIGHTING_SETTINGS.layers, effect: false } } });
    expect((screen.getByRole('checkbox', { name: 'Effects' }) as HTMLInputElement).checked).toBe(false);
    expect((screen.getByRole('checkbox', { name: 'Markers' }) as HTMLInputElement).checked).toBe(true);
  });
});
