import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';
import ListEmptyState from './ListEmptyState';

afterEach(() => { cleanup(); });

describe('list empty state', () => {
  it('shows the indexing message with a spinner while the scan is pending', () => {
    const { container } = render(<ListEmptyState items={0} loading failed={false} />);
    expect(screen.getByRole('status').textContent).toContain('Indexing objects (first run takes up to ~30 s)');
    expect(container.querySelector('.ab-spin')).not.toBeNull();
    expect(screen.queryByText('No objects match the filter.')).toBeNull();
  });

  it('shows no-match only after a finished scan returned zero rows', () => {
    render(<ListEmptyState items={0} loading={false} failed={false} />);
    expect(screen.getByText('No objects match the filter.')).toBeTruthy();
    expect(screen.queryByRole('status')).toBeNull();
  });

  it('shows nothing when the scan failed, because the alert carries the error', () => {
    const { container } = render(<ListEmptyState items={0} loading={false} failed />);
    expect(container.textContent).toBe('');
  });

  it('shows nothing once rows are present', () => {
    const { container } = render(<ListEmptyState items={5} loading={false} failed={false} />);
    expect(container.textContent).toBe('');
  });
});
