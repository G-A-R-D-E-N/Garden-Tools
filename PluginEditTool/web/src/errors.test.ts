import { describe, expect, it } from 'vitest';
import { errorMessage, errorText } from './errors';

const oldErrorText = (e: unknown) => (e as { message?: string } | null)?.message || String(e);

describe('errorText', () => {
  it.each([
    new Error('boom'),
    { message: 'host said no' },
    { message: '' },
    { code: 7 },
    'bare string',
    null,
    undefined,
    42,
  ])('matches the old recordActions helper for %o', (e) => {
    expect(errorText(e)).toBe(oldErrorText(e));
  });

  it('returns undefined when there is no message', () => {
    expect(errorMessage({})).toBeUndefined();
    expect(errorMessage(null)).toBeUndefined();
  });
});
