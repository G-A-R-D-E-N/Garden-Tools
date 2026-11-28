import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { useState } from 'react';
import { afterEach, expect, it } from 'vitest';
import { EditorTabPanel, EditorTabs, type EditorTab } from './EditorTabs';

afterEach(() => cleanup());

const TABS: EditorTab[] = [
  { id: 'alpha', label: 'Alpha', count: 3 },
  { id: 'beta', label: 'Beta' },
  { id: 'gamma', label: 'Gamma', badge: 'problem' },
];

function Harness() {
  const [active, setActive] = useState('alpha');
  return (
    <>
      <EditorTabs tabs={TABS} active={active} onChange={setActive} ariaLabel="Sample sections" idPrefix="demo" />
      {TABS.map(tab => (
        <EditorTabPanel key={tab.id} id={tab.id} idPrefix="demo" active={active === tab.id}>
          <p>{`Body of ${tab.label}`}</p>
        </EditorTabPanel>
      ))}
    </>
  );
}

const tab = (name: RegExp) => screen.getByRole('tab', { name });

it('Detects: keyboard navigation moves the selection and focus between tabs, with Home, End and wrap-around', () => {
  render(<Harness />);
  tab(/^Alpha/).focus();
  fireEvent.keyDown(tab(/^Alpha/), { key: 'ArrowRight' });
  expect(tab(/^Beta/).getAttribute('aria-selected')).toBe('true');
  expect(document.activeElement).toBe(tab(/^Beta/));
  fireEvent.keyDown(tab(/^Beta/), { key: 'End' });
  expect(tab(/^Gamma/).getAttribute('aria-selected')).toBe('true');
  fireEvent.keyDown(tab(/^Gamma/), { key: 'ArrowRight' });
  expect(tab(/^Alpha/).getAttribute('aria-selected')).toBe('true');
  fireEvent.keyDown(tab(/^Alpha/), { key: 'ArrowLeft' });
  expect(tab(/^Gamma/).getAttribute('aria-selected')).toBe('true');
  fireEvent.keyDown(tab(/^Gamma/), { key: 'Home' });
  expect(tab(/^Alpha/).getAttribute('aria-selected')).toBe('true');
  expect(document.activeElement).toBe(tab(/^Alpha/));
});

it('Detects: only the active panel is shown, the inactive panels are hidden from assistive technology', () => {
  render(<Harness />);
  const panels = screen.getAllByRole('tabpanel');
  expect(panels).toHaveLength(1);
  expect(panels[0].textContent).toBe('Body of Alpha');
  fireEvent.click(tab(/^Beta/));
  expect(screen.getAllByRole('tabpanel')).toHaveLength(1);
  expect(screen.getByRole('tabpanel').textContent).toBe('Body of Beta');
  expect(screen.getByText('Body of Alpha')).toBeTruthy();
});

it('Detects: aria-selected follows the active id, and the tab controls the panel with the matching id', () => {
  render(<Harness />);
  expect(tab(/^Alpha/).getAttribute('aria-selected')).toBe('true');
  expect(tab(/^Beta/).getAttribute('aria-selected')).toBe('false');
  expect(tab(/^Alpha/).getAttribute('aria-controls')).toBe('demo-panel-alpha');
  fireEvent.click(tab(/^Beta/));
  expect(tab(/^Alpha/).getAttribute('aria-selected')).toBe('false');
  expect(tab(/^Beta/).getAttribute('aria-selected')).toBe('true');
  expect(screen.getByRole('tabpanel').id).toBe('demo-panel-beta');
  expect(tab(/^Beta/).tabIndex).toBe(0);
  expect(tab(/^Alpha/).tabIndex).toBe(-1);
});

it('Detects: the count chip is shown on a tab that has a count, and the problem badge on a tab that has one', () => {
  render(<Harness />);
  expect(tab(/^Alpha/).textContent).toContain('3');
  expect(tab(/^Beta/).querySelector('.editor-tab-count')).toBeNull();
  expect(tab(/^Gamma/).textContent).toContain('problem');
  expect(screen.getByRole('tablist', { name: 'Sample sections' })).toBeTruthy();
});
