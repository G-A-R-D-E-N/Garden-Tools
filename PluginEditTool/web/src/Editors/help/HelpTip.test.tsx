import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import HelpTip from './HelpTip';

afterEach(cleanup);

it('Detects: help text stays hidden until the ? button gets keyboard focus, so keyboard users get the same help as mouse users', () => {
  render(<HelpTip topicId="alias" />);
  const button = screen.getByRole('button', { name: 'Help: Alias' });
  expect(screen.queryByRole('tooltip')).toBeNull();

  fireEvent.focus(button);

  const tip = screen.getByRole('tooltip');
  expect(tip.textContent).toContain('named slot');
  expect(button.getAttribute('aria-describedby')).toBe(tip.id);
});

it('Detects: Esc does not close the help text, so keyboard users cannot dismiss it over the field they are editing', () => {
  render(<HelpTip topicId="alias" />);
  const button = screen.getByRole('button', { name: 'Help: Alias' });
  fireEvent.focus(button);

  fireEvent.keyDown(button, { key: 'Escape' });

  expect(screen.queryByRole('tooltip')).toBeNull();
  expect(button.getAttribute('aria-describedby')).toBeNull();
});

it('Detects: Esc does not close a help text that opened on hover, so a mouse user who moved onto the ? cannot dismiss it from the keyboard', () => {
  render(<HelpTip topicId="alias" />);
  const button = screen.getByRole('button', { name: 'Help: Alias' });
  fireEvent.mouseEnter(button);
  expect(screen.getByRole('tooltip')).toBeTruthy();

  fireEvent.keyDown(button, { key: 'Escape' });

  expect(screen.queryByRole('tooltip')).toBeNull();
});
