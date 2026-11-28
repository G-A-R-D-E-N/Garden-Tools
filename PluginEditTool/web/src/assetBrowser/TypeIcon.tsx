import type { IconKind } from './categoryTree';

const PATHS: Record<IconKind, string> = {
  static: 'M12 2l9 5v10l-9 5-9-5V7zM3 7l9 5 9-5M12 12v10',
  furniture: 'M3 9h18M5 9v12M19 9v12M7 9V5h10v4',
  door: 'M6 2h12v20H6zM14 12h.01',
  activator: 'M13 2L4 14h7l-1 8 9-12h-7z',
  container: 'M3 7h18v13H3zM3 7l2-4h14l2 4M10 12h4',
  light: 'M9 18h6M10 21h4M12 3a6 6 0 0 0-4 10.5V15h8v-1.5A6 6 0 0 0 12 3z',
  weapon: 'M2 13h12l3-5h5v4h-2l-2 2 2 2h-3l-3-2H2z',
  armor: 'M12 2l8 3v6c0 5-3.5 9-8 11-4.5-2-8-6-8-11V5z',
  ammo: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 8v8',
  misc: 'M3 12V3h9l9 9-9 9zM7.5 7.5h.01',
  terminal: 'M3 4h18v12H3zM8 20h8M12 16v4M7 9l2 2-2 2M11 13h3',
  npc: 'M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM4 21c0-4 4-6 8-6s8 2 8 6',
  flora: 'M4 20c0-9 6-14 16-16-1 10-6 16-16 16zM4 20l8-8',
  hazard: 'M12 3l10 18H2zM12 10v5M12 18h.01',
  generic: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 16h.01M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .8-1 1.4v.8',
};

export default function TypeIcon({ kind, size = 24 }: { kind: IconKind; size?: number }) {
  return (
    <svg
      className="ab-type-icon"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.6}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d={PATHS[kind]} />
    </svg>
  );
}
