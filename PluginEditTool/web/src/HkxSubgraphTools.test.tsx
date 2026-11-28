import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { HkxHost } from './backend';
import HkxSubgraphTools from './HkxSubgraphTools';

const PLUGIN = 'Fallout4.esm';
const DATA = 'C:\\Games\\Fallout 4\\Data';

const reply = {
  plugin: PLUGIN,
  race: '0x0001A18D',
  behavior_graphs: ['Actors\\Character\\Behaviors\\MTBehavior.hkx'],
  path_prefixes: ['Actors\\Character\\Animations\\MT\\Neutral'],
  subgraph_ids: [1012108540513697],
  manifest: {
    id: 1012108540513697,
    source: 'Fallout4 - Animations.ba2',
    path: 'meshes\\animtextdata\\animationfiledata\\1012108540513697.txt',
    behavior_paths: ['Actors\\Character\\Behaviors\\MTBehavior.hkx'],
    animation_paths: ['Actors\\Character\\Animations\\MT\\Neutral\\Idle.hkx', 'Actors\\Character\\Animations\\MT\\Missing'],
    present: 1,
    unresolved: ['Actors\\Character\\Animations\\MT\\Missing'],
  },
  notes: [],
};

afterEach(() => cleanup());

describe('HkxSubgraphTools', () => {
  it('names the subgraph id and the unresolved animation a race resolves to', async () => {
    // Constraint: detects a panel that shows the id but hides a manifest gap, which reads as a clean race.
    const host = { Subgraph: vi.fn(async () => JSON.stringify(reply)) } as unknown as HkxHost;
    render(<HkxSubgraphTools host={host} initialPlugin={PLUGIN} initialDataFolder={DATA} disabled={false} />);
    fireEvent.click(screen.getByText('Resolve subgraph'));
    await waitFor(() => expect(screen.getByLabelText('Subgraph ids').textContent).toContain('1012108540513697'));
    expect(screen.getByLabelText('Manifest summary').textContent).toContain('1 of 2 animations present');
    expect(screen.getByLabelText('Unresolved animations').textContent).toContain('MT\\Missing');
    expect(screen.getByRole('status').textContent).toContain('manifest from Fallout4 - Animations.ba2');
  });

  it('shows the engine refusal for a record that is not a race', async () => {
    // Constraint: detects a refused form id reported as an empty animation set.
    const host = {
      Subgraph: vi.fn(async () => JSON.stringify({ error: '0x800 is a MISC record; an animation set is read from RACE' })),
    } as unknown as HkxHost;
    render(<HkxSubgraphTools host={host} initialPlugin={PLUGIN} initialDataFolder={DATA} disabled={false} />);
    fireEvent.click(screen.getByText('Resolve subgraph'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('an animation set is read from RACE'));
  });

  it('says the host build cannot resolve a subgraph instead of showing none', async () => {
    // Constraint: detects an older host shown as a race with no subgraph ids.
    render(<HkxSubgraphTools host={{} as HkxHost} initialPlugin={PLUGIN} initialDataFolder={DATA} disabled={false} />);
    fireEvent.click(screen.getByText('Resolve subgraph'));
    await waitFor(() => expect(screen.getByRole('alert').textContent).toContain('cannot resolve an animation subgraph'));
  });
});
