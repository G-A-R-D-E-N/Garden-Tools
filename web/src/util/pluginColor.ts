// Stable per-plugin chip color + badge letter, so a plugin reads the same everywhere.
const CHIP_COUNT = 8;

function hashString(s: string): number {
  let h = 0;
  for (let i = 0; i < s.length; i++) {
    h = (h * 31 + s.charCodeAt(i)) | 0;
  }
  return Math.abs(h);
}

/** Returns a CSS custom-property reference, e.g. "var(--chip-3)". */
export function pluginColorVar(name: string): string {
  return `var(--chip-${(hashString(name.toLowerCase()) % CHIP_COUNT) + 1})`;
}

/** Uppercase first alphanumeric char of the plugin name, for the badge. */
export function pluginBadge(name: string): string {
  const m = name.match(/[a-z0-9]/i);
  return (m ? m[0] : '?').toUpperCase();
}
