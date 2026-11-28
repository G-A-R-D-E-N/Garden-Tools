import { describe, expect, it } from 'vitest';
import {
  editableFlagMask,
  flagCheckboxes,
  flagIsSet,
  flagsChangeAllowed,
  formatRawFlags,
  lockChange,
  lockDraftOf,
  lockLevelLabel,
  LOCK_LEVELS,
  parseByte,
  parseRawFlags,
  withFlag,
  type FlagOption,
} from './referenceFlagsLock';

const options: FlagOption[] = [
  { name: 'Persistent', bit: 10, editable: false },
  { name: 'Initially disabled', bit: 11, editable: true },
  { name: 'Open by default', bit: 9, editable: true },
  { name: 'Top bit', bit: 31, editable: true },
];

describe('withFlag', () => {
  it('sets and clears single bits without touching the others', () => {
    expect(withFlag(0x400, 11, true)).toBe(0xc00);
    expect(withFlag(0xc00, 11, false)).toBe(0x400);
    expect(withFlag(0xc00, 10, false)).toBe(0x800);
  });

  it('keeps bit 31 as an unsigned value', () => {
    expect(withFlag(0, 31, true)).toBe(0x80000000);
    expect(withFlag(0x80000000, 31, false)).toBe(0);
    expect(flagIsSet(0x80000000, 31)).toBe(true);
  });

  it('ignores bits outside the 32-bit range', () => {
    expect(withFlag(0x10, 40, true)).toBe(0x10);
    expect(flagIsSet(0x10, 40)).toBe(false);
  });
});

describe('flagCheckboxes', () => {
  it('marks each option as checked from the raw value and keeps editability', () => {
    expect(flagCheckboxes(0x600, options)).toEqual([
      { bit: 10, name: 'Persistent', checked: true, editable: false },
      { bit: 11, name: 'Initially disabled', checked: false, editable: true },
      { bit: 9, name: 'Open by default', checked: true, editable: true },
      { bit: 31, name: 'Top bit', checked: false, editable: true },
    ]);
  });
});

describe('flagsChangeAllowed', () => {
  it('allows changes only to editable named bits', () => {
    expect(editableFlagMask(options)).toBe(0x80000000 + 0x800 + 0x200);
    expect(flagsChangeAllowed(0x400, 0x400 | 0x800, options)).toBe(true);
    expect(flagsChangeAllowed(0x400, 0x400 | 0x1000, options)).toBe(false);
  });

  it('refuses a change to a non-editable named bit', () => {
    expect(flagsChangeAllowed(0x400, 0, options)).toBe(false);
  });
});

describe('raw flags text', () => {
  it('formats as eight uppercase hex digits', () => {
    expect(formatRawFlags(0x400)).toBe('0x00000400');
    expect(formatRawFlags(0x80000000)).toBe('0x80000000');
  });

  it('parses hex with or without the prefix and rejects anything else', () => {
    expect(parseRawFlags('0x400')).toBe(0x400);
    expect(parseRawFlags(' ffffffff ')).toBe(0xffffffff);
    expect(parseRawFlags('0X1F')).toBe(0x1f);
    expect(parseRawFlags('0x')).toBeNull();
    expect(parseRawFlags('123456789')).toBeNull();
    expect(parseRawFlags('12z')).toBeNull();
    expect(parseRawFlags('')).toBeNull();
  });
});

describe('parseByte', () => {
  it('accepts whole numbers from 0 to 255', () => {
    expect(parseByte('0')).toBe(0);
    expect(parseByte(' 255 ')).toBe(255);
    expect(parseByte('256')).toBeNull();
    expect(parseByte('-1')).toBeNull();
    expect(parseByte('1.5')).toBeNull();
    expect(parseByte('')).toBeNull();
  });
});

describe('lock levels', () => {
  it('lists the eleven named levels', () => {
    expect(LOCK_LEVELS.map(entry => entry.level)).toEqual([0, 1, 25, 50, 75, 100, 251, 252, 253, 254, 255]);
  });

  it('labels unnamed levels by number', () => {
    expect(lockLevelLabel(254)).toBe('Inaccessible');
    expect(lockLevelLabel(7)).toBe('Level 7');
  });
});

describe('lock changes', () => {
  it('reads a stored lock into a draft with the key form key', () => {
    expect(lockDraftOf({ level: 255, key: { formKey: '000820:Base.esm' }, flags: 4 }))
      .toEqual({ level: 255, key: '000820:Base.esm', flags: 4 });
    expect(lockDraftOf({ level: 0, key: null, flags: 0 })).toEqual({ level: 0, key: null, flags: 0 });
  });

  it('reports no change when the draft matches the current lock', () => {
    const current = { level: 50, key: null, flags: 1 };
    expect(lockChange(current, { level: 50, key: null, flags: 1 })).toBeUndefined();
  });

  it('reports a new lock, a changed lock and a removal', () => {
    expect(lockChange(null, { level: 0, key: null, flags: 0 })).toEqual({ level: 0, key: null, flags: 0 });
    expect(lockChange({ level: 0, key: null, flags: 0 }, { level: 254, key: null, flags: 0 }))
      .toEqual({ level: 254, key: null, flags: 0 });
    expect(lockChange({ level: 50, key: null, flags: 0 }, null)).toBeNull();
  });

  it('treats a different key as a change', () => {
    const current = { level: 255, key: '000820:Base.esm', flags: 0 };
    expect(lockChange(current, { level: 255, key: null, flags: 0 })).toEqual({ level: 255, key: null, flags: 0 });
  });
});
