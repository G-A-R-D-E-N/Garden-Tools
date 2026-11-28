import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import HelpPage from './HelpPage';

afterEach(cleanup);

it('Detects: typing in Search help hides topics that do not match, so a beginner can find one term in the glossary', () => {
  render(<HelpPage onClose={() => {}} />);
  expect(screen.getByRole('heading', { name: 'Quest stage' })).toBeTruthy();
  expect(screen.getByRole('heading', { name: 'Alias' })).toBeTruthy();

  fireEvent.change(screen.getByLabelText('Search help'), { target: { value: 'named slot' } });

  expect(screen.getByRole('heading', { name: 'Alias' })).toBeTruthy();
  expect(screen.queryByRole('heading', { name: 'Quest stage' })).toBeNull();
  expect(screen.queryByRole('heading', { name: 'Objective' })).toBeNull();
});
