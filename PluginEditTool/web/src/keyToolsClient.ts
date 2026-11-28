import type { NifHost } from './backend';
import './poseHost';

// Invariant: these are the four key blocks NifService::replaceKeyChannel writes; the bool and text key blocks are refused natively.
export const KEY_BLOCK_TYPES = ['NiTransformData', 'NiKeyframeData', 'NiPosData', 'NiFloatData'];

export const INTERPOLATIONS = [
  { value: 1, label: 'Linear' },
  { value: 2, label: 'Quadratic' },
  { value: 3, label: 'TBC' },
  { value: 5, label: 'Constant' },
];

export interface KeyChannelInfo { id: string; label: string; components: number; quaternion: boolean }

const TRANSFORM_CHANNELS: KeyChannelInfo[] = [
  { id: 'translation', label: 'Translation', components: 3, quaternion: false },
  { id: 'rotation_quat', label: 'Rotation (quaternion)', components: 4, quaternion: true },
  { id: 'rotation_x', label: 'Rotation X', components: 1, quaternion: false },
  { id: 'rotation_y', label: 'Rotation Y', components: 1, quaternion: false },
  { id: 'rotation_z', label: 'Rotation Z', components: 1, quaternion: false },
  { id: 'scale', label: 'Scale', components: 1, quaternion: false },
];

export const isKeyBlockType = (type: string): boolean => KEY_BLOCK_TYPES.includes(type);

export function channelsFor(type: string): KeyChannelInfo[] {
  if (type === 'NiTransformData' || type === 'NiKeyframeData') return TRANSFORM_CHANNELS;
  if (type === 'NiPosData') return [{ id: 'data', label: 'Position', components: 3, quaternion: false }];
  if (type === 'NiFloatData') return [{ id: 'data', label: 'Value', components: 1, quaternion: false }];
  return [];
}

// Invariant: mirrors NifService.cpp:keyNumbers; a quaternion key has no tangents, a TBC key adds tension, continuity, bias.
export function keyCount(interpolation: number, components: number, quaternion: boolean): number {
  if (interpolation === 2) return quaternion ? components : components * 3;
  if (interpolation === 3) return components + 3;
  return components;
}

export function keyValueLabels(channel: KeyChannelInfo, interpolation: number): string[] {
  const axes = channel.quaternion ? ['w', 'x', 'y', 'z'] : channel.components === 3 ? ['x', 'y', 'z'] : ['value'];
  if (interpolation === 2 && !channel.quaternion) {
    return [...axes, ...axes.map(a => `fwd ${a}`), ...axes.map(a => `bwd ${a}`)];
  }
  if (interpolation === 3) return [...axes, 'tension', 'continuity', 'bias'];
  return axes;
}

export interface KeyRow { time: string; value: string[] }

export const blankKey = (count: number): KeyRow => ({ time: '0', value: Array.from({ length: count }, () => '0') });

export function resizeKeys(rows: KeyRow[], count: number): KeyRow[] {
  return rows.map(row => ({
    time: row.time,
    value: Array.from({ length: count }, (_, i) => row.value[i] ?? '0'),
  }));
}

const finite = (text: string): number | null => {
  const trimmed = text.trim();
  if (trimmed === '') return null;
  const number = Number(trimmed);
  return Number.isFinite(number) ? number : null;
};

// Requires every cell to be a finite number and the times not to decrease, or returns null so Apply stays disabled.
export function parseKeys(rows: KeyRow[]): { time: number; value: number[] }[] | null {
  if (rows.length === 0) return null;
  const keys: { time: number; value: number[] }[] = [];
  for (const row of rows) {
    const time = finite(row.time);
    const value = row.value.map(finite);
    if (time === null || value.some(v => v === null)) return null;
    if (keys.length > 0 && time < keys[keys.length - 1].time) return null;
    keys.push({ time, value: value as number[] });
  }
  return keys;
}

export interface SetKeysReport { ok: boolean; saved: string; channel: string; keys: number; refusal: string }

const firstErrorLine = (text: string): string => {
  const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
  return (lines[0] ?? '').replace(/^Error:\s*/i, '') + (lines.length > 1 ? ` (${lines.slice(1).join('; ')})` : '');
};

export function parseSetKeysReport(text: string): SetKeysReport {
  const ok = /^set_keys OK/m.test(text);
  const saved = text.match(/Saved: ([^\n]*)/);
  const channel = text.match(/Channel: ([^\n]*)/);
  const keys = text.match(/Keys: (\d+)/);
  return {
    ok,
    saved: saved ? saved[1].trim() : '',
    channel: channel ? channel[1].trim() : '',
    keys: keys ? Number(keys[1]) : 0,
    refusal: ok ? '' : firstErrorLine(text),
  };
}

export interface KeyChannelRead { channel: string; interpolation: number; components: number; keys: { time: number; value: number[] }[] }

const isNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);

const channelFrom = (entry: unknown): KeyChannelRead | null => {
  const c = entry as { channel?: unknown; interpolation?: unknown; components?: unknown; keys?: unknown } | null;
  if (!c || typeof c.channel !== 'string' || !isNumber(c.interpolation) || !isNumber(c.components) || !Array.isArray(c.keys)) return null;
  const keys: KeyChannelRead['keys'] = [];
  for (const raw of c.keys as ({ time?: unknown; value?: unknown } | null)[]) {
    if (!raw || !isNumber(raw.time) || !Array.isArray(raw.value) || !raw.value.every(isNumber)) return null;
    keys.push({ time: raw.time, value: raw.value as number[] });
  }
  return { channel: c.channel, interpolation: c.interpolation, components: c.components, keys };
};

export function parseReadKeys(text: string): { channels: KeyChannelRead[]; error: string } {
  let json: { error?: unknown; channels?: unknown } | null;
  try {
    json = JSON.parse(text);
  } catch {
    return { channels: [], error: firstErrorLine(text) || 'the key reply was empty' };
  }
  if (!json || typeof json !== 'object') return { channels: [], error: 'the key reply was not an object' };
  if (typeof json.error === 'string') return { channels: [], error: json.error };
  if (!Array.isArray(json.channels)) return { channels: [], error: 'the key reply has no channels' };
  const channels: KeyChannelRead[] = [];
  for (const entry of json.channels) {
    const channel = channelFrom(entry);
    if (channel) channels.push(channel);
  }
  return { channels, error: '' };
}

export async function readKeys(host: NifHost, nifPath: string, block: number) {
  return parseReadKeys(await host.ReadKeys(nifPath, block));
}

export function prefillRows(read: KeyChannelRead, channel: KeyChannelInfo): KeyRow[] | null {
  if (read.keys.length === 0 || !INTERPOLATIONS.some(i => i.value === read.interpolation)) return null;
  const count = keyCount(read.interpolation, channel.components, channel.quaternion);
  if (read.keys.some(k => k.value.length !== count)) return null;
  return read.keys.map(k => ({ time: String(k.time), value: k.value.map(String) }));
}

export const isBlankKeys = (rows: KeyRow[]): boolean =>
  rows.length <= 1 && rows.every(row => row.time === '0' && row.value.every(v => v === '0'));

// Invariant: an empty output path saves in place, so the caller reloads the file it already has open.
export async function writeKeys(host: NifHost, nifPath: string, block: number, channel: string, interpolation: number,
                                keys: { time: number; value: number[] }[], expectedSource = ''): Promise<SetKeysReport> {
  const text = await host.BlockOp(nifPath, 'set_keys', block, null, '', null, '', expectedSource, false, null,
    '', '', channel, interpolation, keys);
  return parseSetKeysReport(text);
}
