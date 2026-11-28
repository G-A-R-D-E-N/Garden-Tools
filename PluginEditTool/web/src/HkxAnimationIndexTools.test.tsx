import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxAnimationIndexTools from './HkxAnimationIndexTools';

const DATA = 'C:\\Games\\Fallout 4\\Data';
const WALK = 'meshes/actors/character/animations/walk.hkx';
const HOWL = 'meshes/actors/wolf/animations/howl.hkx';
const SKELETON = 'meshes/actors/character/characterassets/skeleton.hkx';

const WALK_FILE = 'C:\\mods\\High\\Meshes\\Actors\\Character\\Animations\\Walk.hkx';
const SKELETON_FILE = `${DATA}\\Meshes\\Actors\\Character\\CharacterAssets\\Skeleton.hkx`;
const MATCHED_FILE = 'C:\\mods\\High\\Meshes\\Actors\\Character\\CharacterAssets\\Skeleton.hkx';

const provider = (label: string, origin: 'loose' | 'archive' = 'loose', source = `C:\\mods\\${label}`, file = '') =>
  ({ label, origin, source, file });

const facet = (values: string[], hidden = 0) => ({ values, hidden });

const entry = (path: string, kind: string, winner: ReturnType<typeof provider>, shadowed: ReturnType<typeof provider>[] = []) =>
  ({ path, kind, project: 'character', race: 'character', character: '', folder: '', size: 4, provider: winner, shadowed });

const reply = (over: Record<string, unknown> = {}) => ({
  manager: 'mo2',
  profile: 'Test',
  data_folder: DATA,
  sources: [provider('High'), provider('Data', 'loose', DATA), provider('Pack.ba2', 'archive', 'C:\\mods\\High\\Pack.ba2')],
  notes: [],
  total: 3,
  counts: { animation: 2, skeleton: 1 },
  matched: 3,
  offset: 0,
  limit: 200,
  truncated: false,
  entries: [
    entry(WALK, 'animation', provider('High', 'loose', 'C:\\mods\\High', WALK_FILE), [provider('Data', 'loose', DATA)]),
    entry(SKELETON, 'skeleton', provider('Data', 'loose', DATA, SKELETON_FILE)),
    entry(HOWL, 'animation', provider('Base.ba2', 'archive', `${DATA}\\Base.ba2`)),
  ],
  facets: { projects: facet(['character', 'wolf']), races: facet(['character']), characters: facet([]) },
  ...over,
});

const hostOf = (fn: unknown) => ({ AnimationIndex: fn }) as unknown as HkxHost;

const renderTools = (host: HkxHost, extra: Partial<Parameters<typeof HkxAnimationIndexTools>[0]> = {}) =>
  render(<HkxAnimationIndexTools host={host} disabled={false}
    onPickSkeleton={vi.fn()} onPickAnimation={vi.fn()} {...extra} />);

const scan = async () => {
  fireEvent.change(screen.getByLabelText('Index Data folder'), { target: { value: DATA } });
  fireEvent.click(screen.getByText('Index animations'));
  await waitFor(() => expect(screen.getByRole('status')).toBeTruthy());
};

afterEach(() => cleanup());

describe('HkxAnimationIndexTools', () => {
  it('names the winning provider and the providers it shadows', async () => {
    // Constraint: detects a list that shows the path but hides which mod or archive wins it.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify(reply()));
    renderTools(hostOf(call));
    await scan();
    const list = screen.getByLabelText('Animation index entries').textContent ?? '';
    expect(list).toContain(`${WALK} [animation] from High, shadows Data`);
    expect(list).toContain(`${HOWL} [animation] from Base.ba2 (archive)`);
    expect(screen.getByLabelText('Index sources').textContent).toContain('High > Data > Pack.ba2');
    expect(screen.getByRole('status').textContent).toContain('mo2 profile Test: 1-3 of 3 matching, 3 indexed');
  });

  it('rescans on Index but reuses the kept index on Filter and pages with the next offset', async () => {
    // Constraint: detects a filter that rescans the disk, an Index click that serves a stale index, or a page that restarts at zero.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify(reply({ truncated: true })));
    renderTools(hostOf(call));
    await scan();
    expect(call.mock.calls[0][11]).toBe(true);
    expect(call.mock.calls[0][9]).toBe(0);

    fireEvent.change(screen.getByLabelText('Index kind'), { target: { value: 'skeleton' } });
    fireEvent.click(screen.getByText('Filter'));
    await waitFor(() => expect(call).toHaveBeenCalledTimes(2));
    expect(call.mock.calls[1][3]).toBe('skeleton');
    expect(call.mock.calls[1][11]).toBe(false);

    await waitFor(() => expect(screen.getByText('Next page').hasAttribute('disabled')).toBe(false));
    fireEvent.click(screen.getByText('Next page'));
    await waitFor(() => expect(call).toHaveBeenCalledTimes(3));
    expect(call.mock.calls[2][9]).toBe(200);
    expect(call.mock.calls[2][11]).toBe(false);
  });

  it('opens a loose winner in place and extracts an archive winner before handing it to the loader', async () => {
    // Constraint: detects an archive-only path handed straight to the loose loaders, or an extraction asked with a different source set than the index used.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify(reply()));
    const extract = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify({
      path: 'C:/Temp/FO4RE_Hkx/abc.hkx', extracted: true, asset: HOWL, provider: provider('Base.ba2', 'archive'), size: 4,
    }));
    const onPickAnimation = vi.fn();
    const onPickSkeleton = vi.fn();
    renderTools({ AnimationIndex: call, ExtractAnimationAsset: extract } as unknown as HkxHost, { onPickAnimation, onPickSkeleton });
    await scan();
    fireEvent.click(screen.getByLabelText(`Use animation ${WALK}`));
    await waitFor(() => expect(onPickAnimation).toHaveBeenCalledWith(WALK_FILE));
    fireEvent.click(screen.getByLabelText(`Use skeleton ${SKELETON}`));
    await waitFor(() => expect(onPickSkeleton).toHaveBeenCalledWith(SKELETON_FILE));
    expect(extract).not.toHaveBeenCalled();

    fireEvent.click(screen.getByLabelText(`Use animation ${HOWL}`));
    await waitFor(() => expect(onPickAnimation).toHaveBeenCalledWith('C:/Temp/FO4RE_Hkx/abc.hkx'));
    expect(extract).toHaveBeenCalledWith(DATA, '', '', HOWL);
  });

  it('shows the extraction refusal and loads nothing when an archive file cannot be extracted', async () => {
    // Constraint: detects a failed extraction that still hands a path to the loader or leaves the failure silent.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify(reply()));
    const extract = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify({ error: 'not found in any source' }));
    const onPickAnimation = vi.fn();
    renderTools({ AnimationIndex: call, ExtractAnimationAsset: extract } as unknown as HkxHost, { onPickAnimation });
    await scan();
    fireEvent.click(screen.getByLabelText(`Use animation ${HOWL}`));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('not found in any source'));
    expect(onPickAnimation).not.toHaveBeenCalled();
  });

  it('extracts the matched body NIF and passes the Data folder as the texture root', async () => {
    // Constraint: detects a body shown from a path the host cannot read, or textures looked up under the wrong root.
    const actor = {
      actor_root: 'meshes/actors/character', skeleton: null, skeleton_provider: null, skeleton_candidates: [],
      skeleton_nif: null, skeleton_nif_provider: null,
      body: 'meshes/actors/character/body.nif', body_provider: provider('Bodies.ba2', 'archive'),
      body_candidates: [], body_candidate_total: 1, notes: [],
    };
    const call = vi.fn(async (...args: unknown[]) => JSON.stringify(reply(args[8] ? { actor } : {})));
    const extract = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify({
      path: 'C:/Temp/FO4RE_Hkx/body.nif', extracted: true, asset: actor.body, provider: actor.body_provider, size: 9,
    }));
    const onPickBody = vi.fn();
    renderTools({ AnimationIndex: call, ExtractAnimationAsset: extract } as unknown as HkxHost, { onPickBody });
    await scan();
    fireEvent.click(screen.getByLabelText(`Match actor ${WALK}`));
    await waitFor(() => expect(screen.getByLabelText(`Show body ${actor.body}`)).toBeTruthy());
    fireEvent.click(screen.getByLabelText(`Show body ${actor.body}`));
    await waitFor(() => expect(onPickBody).toHaveBeenCalledWith('C:/Temp/FO4RE_Hkx/body.nif', DATA));
  });

  it('shows the matched skeleton and its provider, and lists candidates when none is chosen', async () => {
    // Constraint: detects an ambiguous actor shown with a guessed skeleton or body instead of the candidate list.
    const actor = {
      actor_root: 'meshes/actors/wolf',
      skeleton: null,
      skeleton_provider: null,
      skeleton_candidates: ['meshes/actors/wolf/characterassets/skeleton_a.hkx', 'meshes/actors/wolf/characterassets/skeleton_b.hkx'],
      skeleton_nif: null,
      skeleton_nif_provider: null,
      body: null,
      body_provider: null,
      body_candidates: ['meshes/actors/wolf/body1.nif', 'meshes/actors/wolf/body2.nif'],
      body_candidate_total: 3,
      notes: ['2 skeleton candidates sit under meshes/actors/wolf/characterassets/ and none is named skeleton alone, so none is chosen'],
    };
    const call = vi.fn(async (...args: unknown[]) => JSON.stringify(reply(args[8] ? { actor } : {})));
    renderTools(hostOf(call));
    await scan();
    fireEvent.click(screen.getByLabelText(`Match actor ${HOWL}`));
    await waitFor(() => expect(screen.getByLabelText('Actor assets')).toBeTruthy());
    expect(call.mock.calls[1][8]).toBe(HOWL);
    expect(call.mock.calls[1][11]).toBe(false);
    expect(screen.getByLabelText('Actor assets').textContent).toContain('Skeleton: none chosen');
    expect(screen.getByLabelText('Skeleton candidates').textContent).toContain('skeleton_b.hkx');
    expect(screen.getByLabelText('Body candidates').textContent).toContain('1 more not listed');
    expect(screen.getByLabelText('Actor notes').textContent).toContain('none is chosen');
  });

  it('passes a matched loose skeleton to the skeleton loader', async () => {
    // Constraint: detects a matched skeleton that is shown but cannot be sent to the clip loader.
    const actor = {
      actor_root: 'meshes/actors/character',
      skeleton: SKELETON,
      skeleton_provider: provider('High', 'loose', 'C:\\mods\\High', MATCHED_FILE),
      skeleton_candidates: [SKELETON],
      skeleton_nif: null,
      skeleton_nif_provider: null,
      body: 'meshes/actors/character/body.nif',
      body_provider: provider('Data', 'loose', DATA),
      body_candidates: ['meshes/actors/character/body.nif'],
      body_candidate_total: 1,
      notes: [],
    };
    const call = vi.fn(async (...args: unknown[]) => JSON.stringify(reply(args[8] ? { actor } : {})));
    const onPickSkeleton = vi.fn();
    renderTools(hostOf(call), { onPickSkeleton });
    await scan();
    fireEvent.click(screen.getByLabelText(`Match actor ${WALK}`));
    await waitFor(() => expect(screen.getByLabelText('Actor assets')).toBeTruthy());
    expect(screen.getByLabelText('Actor assets').textContent).toContain(`Skeleton: ${SKELETON} (from High)`);
    await waitFor(() => expect(screen.getByText('Use matched skeleton').hasAttribute('disabled')).toBe(false));
    fireEvent.click(screen.getByText('Use matched skeleton'));
    await waitFor(() => expect(onPickSkeleton).toHaveBeenCalledWith(MATCHED_FILE));
  });

  it('shows the engine refusal instead of an empty index', async () => {
    // Constraint: detects a refused MO2 folder or missing Data folder shown as an index with no files.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify({ error: 'not a Mod Organizer 2 instance. Vortex mod lists are not supported' }));
    renderTools(hostOf(call));
    fireEvent.change(screen.getByLabelText('MO2 instance folder'), { target: { value: 'C:\\Vortex' } });
    fireEvent.click(screen.getByText('Index animations'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('Vortex mod lists are not supported'));
    expect(screen.queryByLabelText('Animation index entries')).toBeNull();
  });

  it('shows the index notes and says when a host build cannot index', async () => {
    // Constraint: detects a skipped archive or Vortex note dropped from the panel, and an older host shown as an empty index.
    const call = vi.fn<(...args: unknown[]) => Promise<string>>(async () => JSON.stringify(reply({ manager: 'vortex', profile: '', notes: ['a Vortex deployment was found'] })));
    const first = renderTools(hostOf(call));
    await scan();
    expect(screen.getByLabelText('Index notes').textContent).toContain('a Vortex deployment was found');
    first.unmount();

    renderTools({} as HkxHost);
    fireEvent.change(screen.getByLabelText('Index Data folder'), { target: { value: DATA } });
    fireEvent.click(screen.getByText('Index animations'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot index animations'));
  });
});
