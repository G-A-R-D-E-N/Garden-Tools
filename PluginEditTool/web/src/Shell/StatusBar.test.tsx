import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import StatusBar from './StatusBar';
import { mo2FreshnessLabel } from './mo2Freshness';

describe('StatusBar', () => {
  it('reports current and stale MO2 config or load-order state', () => {
    expect(mo2FreshnessLabel({ loaded: true, profile: 'Smoke' })).toBe('MO2 Smoke current');
    expect(mo2FreshnessLabel({
      loaded: true, stale: true, staleConfig: true, profile: 'Smoke',
    })).toBe('MO2 Smoke config changed');
    expect(mo2FreshnessLabel({
      loaded: true, stale: true, staleLoadOrder: true, profile: 'Smoke',
    })).toBe('MO2 Smoke load order changed');
    expect(mo2FreshnessLabel({ loaded: false, stale: true })).toBeNull();

    render(
      <StatusBar
        status=""
        progress={null}
        recordCount={0}
        visibleCount={0}
        selectedCount={0}
        loadOrder={[]}
        mo2Loaded
        mo2Stale
        mo2StaleLoadOrder
        mo2Profile="Smoke"
      />,
    );

    expect(screen.getByText('MO2 Smoke load order changed')).toBeTruthy();
  });
});
