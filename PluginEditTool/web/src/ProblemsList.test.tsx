import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';
import type { RecordProblem } from './backend';
import ProblemsList from './ProblemsList';

const link: RecordProblem = {
  Code: 'unresolved_link', Severity: 'critical', Description: 'Missing WEAP FormLink target 000999:Base.esp',
};
const preflight: RecordProblem = {
  Code: 'schema_preflight', Severity: 'warning', Description: 'REFR\\Scale: value 12 out of range',
};

afterEach(cleanup);

describe('ProblemsList', () => {
  it('shows the stable code on each problem row', () => {
    render(<ProblemsList problems={[link, preflight]} />);
    expect(screen.getByText(link.Description).closest('.rv-prob-row')?.textContent).toContain('unresolved_link');
    expect(screen.getByText(preflight.Description).closest('.rv-prob-row')?.textContent).toContain('schema_preflight');
  });

  it('filters rows by code and hides the other codes', () => {
    render(<ProblemsList problems={[link, preflight]} />);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'schema_preflight' } });
    expect(screen.queryByText(link.Description)).toBeNull();
    expect(screen.getByText(preflight.Description)).toBeTruthy();
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'all' } });
    expect(screen.getByText(link.Description)).toBeTruthy();
  });
});
