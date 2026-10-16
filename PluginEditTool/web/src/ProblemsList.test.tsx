import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';
import type { RecordProblem } from './backend';
import ProblemsList from './ProblemsList';

const link: RecordProblem = {
  Code: 'unresolved_link', Detail: '', Severity: 'critical', Description: 'Missing WEAP FormLink target 000999:Base.esp',
};
const preflight: RecordProblem = {
  Code: 'schema_preflight', Detail: 'scalar_value_out_of_range', Severity: 'warning', Description: 'REFR\\Scale: value 12 out of range',
};

afterEach(cleanup);

describe('ProblemsList', () => {
  it('shows the stable code and severity class on each problem row', () => {
    render(<ProblemsList problems={[link, preflight]} />);
    expect(screen.getByText(link.Description).closest('.rv-prob-row')?.textContent).toContain('unresolved_link');
    expect(screen.getByText(preflight.Description).closest('.rv-prob-row')?.textContent).toContain('schema_preflight');
    expect(screen.getByText(preflight.Description).closest('.rv-prob-row')?.classList.contains('rv-prob-warning')).toBe(true);
  });

  it('filters rows by code and hides the other codes', () => {
    render(<ProblemsList problems={[link, preflight]} />);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'schema_preflight' } });
    expect(screen.queryByText(link.Description)).toBeNull();
    expect(screen.getByText(preflight.Description)).toBeTruthy();
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'all' } });
    expect(screen.getByText(link.Description)).toBeTruthy();
  });

  it('shows the failure text instead of the rows when the check fails', () => {
    render(<ProblemsList problems={[link]} error="boom" />);
    expect(screen.getByText(/Problem check failed/)).toBeTruthy();
    expect(screen.getByText(/boom/)).toBeTruthy();
    expect(screen.queryByText(link.Description)).toBeNull();
  });

  it('resets the code filter to all when the record key changes', () => {
    const { rerender } = render(<ProblemsList key="A" problems={[link, preflight]} />);
    fireEvent.change(screen.getByRole('combobox'), { target: { value: 'schema_preflight' } });
    expect(screen.queryByText(link.Description)).toBeNull();
    rerender(<ProblemsList key="B" problems={[link, preflight]} />);
    expect((screen.getByRole('combobox') as HTMLSelectElement).value).toBe('all');
    expect(screen.getByText(link.Description)).toBeTruthy();
  });

  it('shows the detail next to the code when it is set', () => {
    render(<ProblemsList problems={[preflight]} />);
    expect(screen.getByText('scalar_value_out_of_range')).toBeTruthy();
  });
});
