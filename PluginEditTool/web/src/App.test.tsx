import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import App from './App';

vi.mock('./MainShell', () => ({ default: () => 'main shell stub' }));

afterEach(() => { cleanup(); window.location.hash = ''; });

describe('App routes', () => {
  it('redirects the bare root to the main shell route', async () => {
    window.location.hash = '#/';
    render(<App />);
    expect(await screen.findByText('main shell stub')).toBeTruthy();
    expect(window.location.hash).toBe('#/main');
  });
});
