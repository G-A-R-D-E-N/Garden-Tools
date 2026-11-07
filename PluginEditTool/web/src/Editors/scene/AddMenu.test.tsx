import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import AddMenu from './AddMenu';

afterEach(() => { cleanup(); });

const handlers = () => ({ onAddPhase: vi.fn(), onAddActor: vi.fn(), onAddAction: vi.fn() });

const renderMenu = (phaseCount = 2) => {
  const fns = handlers();
  render(<AddMenu phaseCount={phaseCount} {...fns} />);
  return fns;
};

const trigger = () => screen.getByRole('button', { name: 'Add' });
const item = (name: string | RegExp) => screen.getByRole('menuitem', { name });

function openMenu() {
  fireEvent.click(trigger());
  return screen.getByRole('menu');
}

it('Detects: opening the Add menu focuses its first item', () => {
  renderMenu();
  openMenu();
  expect(document.activeElement).toBe(item('Add phase'));
});

it('Detects: arrow, Home and End keys move focus through the top-level items and wrap around', () => {
  renderMenu();
  openMenu();
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowDown' });
  expect(document.activeElement).toBe(item('Add actor'));
  fireEvent.keyDown(item('Add actor'), { key: 'ArrowDown' });
  expect(document.activeElement).toBe(item(/^Add action/));
  fireEvent.keyDown(item(/^Add action/), { key: 'ArrowDown' });
  expect(document.activeElement).toBe(item('Add phase'));
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowUp' });
  expect(document.activeElement).toBe(item(/^Add action/));
  fireEvent.keyDown(item(/^Add action/), { key: 'Home' });
  expect(document.activeElement).toBe(item('Add phase'));
  fireEvent.keyDown(item('Add phase'), { key: 'End' });
  expect(document.activeElement).toBe(item(/^Add action/));
});

it('Detects: only the focused top-level item is reachable by Tab (roving tabindex)', () => {
  renderMenu();
  openMenu();
  expect(item('Add phase').tabIndex).toBe(0);
  expect(item('Add actor').tabIndex).toBe(-1);
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowDown' });
  expect(item('Add actor').tabIndex).toBe(0);
  expect(item('Add phase').tabIndex).toBe(-1);
});

it('Detects: ArrowRight opens the action type submenu and focuses its first type', () => {
  renderMenu();
  openMenu();
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowDown' });
  fireEvent.keyDown(item('Add actor'), { key: 'ArrowDown' });
  fireEvent.keyDown(item(/^Add action/), { key: 'ArrowRight' });
  expect(screen.getByRole('group', { name: 'Action type' })).toBeTruthy();
  expect(document.activeElement).toBe(item('Dialogue'));
});

it('Detects: ArrowLeft in the submenu closes it and returns focus to Add action', () => {
  renderMenu();
  openMenu();
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowDown' });
  fireEvent.keyDown(item('Add actor'), { key: 'ArrowDown' });
  fireEvent.keyDown(item(/^Add action/), { key: 'ArrowRight' });
  fireEvent.keyDown(item('Dialogue'), { key: 'ArrowLeft' });
  expect(screen.queryByRole('group', { name: 'Action type' })).toBeNull();
  expect(screen.getByRole('menu')).toBeTruthy();
  expect(document.activeElement).toBe(item(/^Add action/));
});

it('Detects: Escape closes the submenu first, then the menu, and refocuses the trigger', () => {
  renderMenu();
  openMenu();
  fireEvent.keyDown(item('Add phase'), { key: 'ArrowDown' });
  fireEvent.keyDown(item('Add actor'), { key: 'ArrowDown' });
  fireEvent.keyDown(item(/^Add action/), { key: 'ArrowRight' });
  fireEvent.keyDown(item('Dialogue'), { key: 'Escape' });
  expect(screen.queryByRole('group', { name: 'Action type' })).toBeNull();
  expect(screen.getByRole('menu')).toBeTruthy();
  expect(document.activeElement).toBe(item(/^Add action/));
  fireEvent.keyDown(item(/^Add action/), { key: 'Escape' });
  expect(screen.queryByRole('menu')).toBeNull();
  expect(document.activeElement).toBe(trigger());
});

it('Detects: moving focus outside the Add menu closes it', () => {
  renderMenu();
  openMenu();
  fireEvent.focusOut(item('Add phase'), { relatedTarget: document.body });
  expect(screen.queryByRole('menu')).toBeNull();
});

it('Detects: focus moving to another item inside the Add menu keeps it open', () => {
  renderMenu();
  openMenu();
  fireEvent.focusOut(item('Add phase'), { relatedTarget: item('Add actor') });
  expect(screen.getByRole('menu')).toBeTruthy();
});

it('Detects: Add action is aria-disabled with a visible reason when the scene has no phase, and does not open the submenu', () => {
  renderMenu(0);
  openMenu();
  const addAction = item(/^Add action/);
  expect(addAction.getAttribute('aria-disabled')).toBe('true');
  expect(addAction.textContent).toContain('Add a phase first');
  fireEvent.click(addAction);
  expect(screen.queryByRole('group', { name: 'Action type' })).toBeNull();
});

it('Detects: adding a phase calls the handler once and refocuses the trigger', () => {
  const fns = renderMenu();
  openMenu();
  fireEvent.click(item('Add phase'));
  expect(fns.onAddPhase).toHaveBeenCalledTimes(1);
  expect(screen.queryByRole('menu')).toBeNull();
  expect(document.activeElement).toBe(trigger());
});
