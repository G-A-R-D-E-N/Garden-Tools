import type { HkxAnimationInspect } from './backend';

export interface TrackRow { frame: number; time: number; position: string; rotation: string; scale: string }

const list = (values: number[], at: number, size: number, digits: number) =>
  values.slice(at * size, at * size + size).map(value => value.toFixed(digits)).join(', ');

// Constraint: the scale column is empty for a track whose scale never leaves 1, as in the BGS Animation tab.
export function trackRows(reply: HkxAnimationInspect): { rows: TrackRow[]; scaled: boolean } {
  const page = reply.track;
  if (!page) throw new Error('the host returned no track page');
  const step = reply.frame_duration ?? 0;
  const scaled = page.scales.some(value => Math.abs(value - 1) > 1e-6);
  const rows = Array.from({ length: page.shown }, (_, frame) => ({
    frame,
    time: frame * step,
    position: list(page.translations, frame, 3, 3),
    rotation: list(page.rotations, frame, 4, 4),
    scale: scaled ? list(page.scales, frame, 3, 4) : '',
  }));
  return { rows, scaled };
}

export interface NamedTrack { index: number; name: string }

// Constraint: the filter is a case-insensitive substring of the track name as in the BGS Animation tab; blank keeps every track.
export function filterTracks(reply: HkxAnimationInspect, text: string): NamedTrack[] {
  const needle = text.trim().toLowerCase();
  return (reply.tracks ?? [])
    .map(track => ({ index: track.index, name: track.name ?? `track ${track.index}` }))
    .filter(track => !needle || track.name.toLowerCase().includes(needle));
}
