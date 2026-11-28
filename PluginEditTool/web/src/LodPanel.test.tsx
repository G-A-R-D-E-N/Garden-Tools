import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { LodHost } from './backend';
import LodPanel from './LodPanel';
import { levelOptions, tilesFor } from './lodClient';

vi.mock('./NifViewport', () => ({ default: () => null }));

const WORLD_TILES = [
  { x: 0, line: 'terrain 32 0 0 4' },
  { x: 1, line: 'terrain 32 1 0 9' },
];

function installHost(failing: number[] = [], recorded = 'terrain 32 1 0 9\n'): LodHost {
  const host: LodHost = {
    Worlds: vi.fn(async () => JSON.stringify([
      { name: 'Commonwealth', southwestX: -96, southwestY: -96, stride: 256, levelMin: 4, levelMax: 32 },
    ])),
    Plan: vi.fn(async () => JSON.stringify({
      plan: WORLD_TILES.map(tile => ({
        world: 'commonwealth', kind: 'terrain', level: 32, x: tile.x, y: 0, inputs: tile.x === 0 ? 4 : 9,
        rebuild: !recorded.includes(tile.line),
      })),
      ledger: recorded,
      commit: WORLD_TILES.filter(tile => !recorded.includes(tile.line)).map(tile => tile.line + '\n').join(''),
    })),
    Preview: vi.fn(async () => 'C:\\temp\\garden-lod-tile.btr'),
    Commit: vi.fn(async () => 'terrain 32 0 0 4\nterrain 32 1 0 9\n'),
    // Constraint: the mock rebuilds a tile while the ledger lacks its line.
    Candidates: vi.fn(async () => JSON.stringify({
      world: 'Commonwealth',
      level: 8,
      tiles: 2,
      counts: { referencesSeen: 5, included: 3, skipped: { noMesh: 2 } },
      candidates: [],
    })),
    VerifyWorld: vi.fn(async (_plugins: string, _data: string, _archives: string, _world: string, ledger: string) =>
      host.Verify(_plugins, _data, _archives, ledger)),
    Verify: vi.fn(async (_plugins: string, _data: string, _archives: string, ledger: string) => JSON.stringify({
      missing: WORLD_TILES.filter(tile => !ledger.includes(tile.line)).length,
      ok: WORLD_TILES.filter(tile => ledger.includes(tile.line)).length,
      tiles: WORLD_TILES.map(tile => ({
        world: 'Commonwealth', kind: 'terrain', level: 32, x: tile.x, y: 0,
        state: ledger.includes(tile.line) ? 'ok' : 'missing',
        local: ledger.includes(tile.line) ? 'present' : 'missing',
        ledger: ledger.includes(tile.line) ? 'current' : 'stale',
        bytes: 4096,
        inArchive: false,
      })),
      textures: [],
    })),
    GenerateWorld: vi.fn(async (_plugins: string, _data: string, _archives: string, _world: string, ledger: string,
                                 apply: boolean) => host.Generate(_plugins, _data, _archives, ledger, apply)),
    // Constraint: the scoped mock ignores the level range and rebuilds from the ledger like Generate does.
    GenerateScoped: vi.fn(async (_plugins: string, _data: string, _archives: string, _world: string, ledger: string,
                                 apply: boolean) =>
      host.Generate(_plugins, _data, _archives, ledger, apply)),
    Progress: vi.fn(async () => JSON.stringify({ done: 0, total: 0, kind: '', level: 0 })),
    CancelScoped: vi.fn(),
    Generate: vi.fn(async (_plugins: string, _data: string, _archives: string, ledger: string, apply: boolean) => {
      const pending = WORLD_TILES.filter(tile => !ledger.includes(tile.line));
      const status = (tile: { x: number }) =>
        (!apply ? 'would-write' : failing.includes(tile.x) ? 'failed' : 'written');
      const tiles = pending.map(tile => ({
        world: 'Commonwealth', kind: 'terrain', level: 32, x: tile.x, y: 0,
        status: status(tile), bytes: status(tile) === 'written' ? 4096 : 0,
        ...(status(tile) === 'failed' ? { error: 'replacing the tile failed: access denied' } : {}),
      }));
      const written = pending.filter(tile => status(tile) === 'written');
      return JSON.stringify({
        report: {
          apply,
          written: written.length,
          wouldWrite: apply ? 0 : pending.length,
          unsupportedCandidates: 2,
          // Invariant: the panel lists the drop reasons.
          objectDrops: { unsupported_base: 1, malformed: 1 },
          failed: tiles.length - written.length - (apply ? 0 : pending.length),
          unreadableWorlds: 0,
          tiles,
        },
        ledger: WORLD_TILES.filter(tile => ledger.includes(tile.line) || written.includes(tile))
          .map(tile => tile.line + '\n').join(''),
      });
    }),
  };
  Object.assign(window, { chrome: { webview: { hostObjects: { lod: host } } } });
  return host;
}

afterEach(() => {
  cleanup();
  localStorage.clear();
  Reflect.deleteProperty(window, 'chrome');
});

describe('levelOptions', () => {
  it('offers only the doubling levels a world covers', () => {
    expect(levelOptions({ name: 'A', southwestX: 0, southwestY: 0, stride: 256, levelMin: 8, levelMax: 32 }))
      .toEqual([8, 16, 32]);
    expect(levelOptions({ name: 'B', southwestX: 0, southwestY: 0, stride: 128, levelMin: 4, levelMax: 0 }))
      .toEqual([4]);
    expect(levelOptions(null)).toEqual([]);
  });
});

describe('tilesFor', () => {
  it('matches the plan world case-insensitively and keeps only the chosen level', () => {
    const plan = {
      plan: [
        { world: 'commonwealth', kind: 'terrain', level: 32, x: 0, y: 0, inputs: 1, rebuild: true },
        { world: 'commonwealth', kind: 'terrain', level: 16, x: 0, y: 0, inputs: 1, rebuild: true },
      ],
      ledger: '', commit: '',
    };
    expect(tilesFor(plan, 'Commonwealth', 32)).toHaveLength(1);
    expect(tilesFor(plan, 'Commonwealth', 8)).toHaveLength(0);
  });
});

describe('LodPanel verify', () => {
  it('judges the tiles on disk against the stored ledger and lists the ones that need action', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    localStorage.setItem('lod.ledger', 'terrain 32 0 0 4\n');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });

    fireEvent.click(screen.getByText('Verify tiles'));
    await screen.findByText('1 ok, 1 missing');
    expect(host.Verify).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'terrain 32 0 0 4\n');
    const rows = screen.getByLabelText('Verify rows').querySelectorAll('.lod-drop-row');
    expect(Array.from(rows, row => row.textContent)).toEqual(['Commonwealthterrain 32 1,0missing']);
  });
});

describe('LodPanel candidates', () => {
  it('lists the objects that cannot enter object LOD, by reason', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });

    fireEvent.click(screen.getByText('Candidates'));
    await screen.findByText('3 of 5 reference(s) enter 2 tile(s)');
    expect(host.Candidates).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'Commonwealth',
                                                  expect.any(Number));
    const rows = screen.getByLabelText('Candidate skips').querySelectorAll('.lod-drop-row');
    expect(Array.from(rows, row => row.textContent)).toEqual(['noMesh2']);
  });
});

describe('LodPanel level scope', () => {
  // Detects: a level bake that must write only its level, a progress line that must appear, and a Cancel button that
  // must reach the host, so a user can stop a long bake and see it move.
  it('bakes only the selected level, shows host progress and cancels through the host', async () => {
    const host = installHost();
    let release: (text: string) => void = () => {};
    host.GenerateScoped = vi.fn(() => new Promise<string>(resolve => { release = resolve; }));
    host.Progress = vi.fn(async () => JSON.stringify({ done: 1, total: 2, kind: 'terrain', level: 32 }));
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText(/planned \d+ tile\(s\)/);

    fireEvent.click(screen.getByText('Generate level'));
    await screen.findByText('1 of 2 tile(s) processed (terrain)', undefined, { timeout: 4000 });
    expect(host.GenerateScoped).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'Commonwealth',
                                                     expect.any(String), true, 32, 32);

    fireEvent.click(screen.getByText('Cancel'));
    expect(host.CancelScoped).toHaveBeenCalledTimes(1);

    release(JSON.stringify({
      report: {
        apply: true, cancelled: true, written: 1,
        tiles: [{ world: 'Commonwealth', kind: 'terrain', level: 32, x: 0, y: 0, status: 'written', bytes: 4096 }],
      },
      ledger: 'terrain 32 0 0 4\n',
    }));
    await screen.findByText(/cancelled after 1 tile\(s\)/);
    expect(localStorage.getItem('lod.ledger')).toBe('terrain 32 0 0 4\n');
  });

  // Detects (invariant): a stale scoped progress line must not survive the run that resolved.
  it('clears the scoped progress line and the Cancel button once the run resolves', async () => {
    const host = installHost();
    let release: (text: string) => void = () => {};
    host.GenerateScoped = vi.fn(() => new Promise<string>(resolve => { release = resolve; }));
    host.Progress = vi.fn(async () => JSON.stringify({ done: 1, total: 2, kind: 'terrain', level: 32 }));
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText(/planned \d+ tile\(s\)/);
    expect(screen.queryByText('Cancel')).toBeNull();

    fireEvent.click(screen.getByText('Generate level'));
    await screen.findByText('1 of 2 tile(s) processed (terrain)', undefined, { timeout: 4000 });

    release(JSON.stringify({
      report: { apply: true, written: 1, tiles: [{ world: 'Commonwealth', kind: 'terrain', level: 32, x: 0, y: 0,
        status: 'written', bytes: 4096 }] },
      ledger: 'terrain 32 0 0 4\n',
    }));
    await screen.findByLabelText('Generate report');
    await waitFor(() => expect(screen.queryByLabelText('Scoped progress')).toBeNull());
    expect(screen.queryByText('Cancel')).toBeNull();
  });
});

describe('LodPanel world scope', () => {
  it('bakes and verifies only the selected world', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText(/planned \d+ tile\(s\)/);

    fireEvent.click(screen.getByText('Generate world'));
    await screen.findByLabelText('Generate report');
    expect(host.GenerateWorld).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'Commonwealth',
                                                    expect.any(String), true);

    fireEvent.click(screen.getByText('Verify world'));
    await screen.findByText('2 ok');
    expect(host.VerifyWorld).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'Commonwealth',
                                                  'terrain 32 0 0 4\nterrain 32 1 0 9\n');
  });
});

describe('LodPanel rebake plan', () => {
  it('plans the selected world and level, reports the pending count and records a baked ledger', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });

    fireEvent.click(screen.getByText('Plan rebakes'));
    const record = screen.getByText('Record baked tiles').closest('button')!;
    await screen.findByText('1 of 2 tile(s) need a rebake');
    expect(host.Plan).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', '');

    await screen.findByText('1 of 2 tile(s) need a rebake');
    expect(record.disabled).toBe(false);

    fireEvent.click(record);
    await screen.findByText('Recorded the baked tiles in the ledger.');
    expect(host.Commit).toHaveBeenCalledWith('terrain 32 1 0 9\n', 'terrain 32 0 0 4\n');
    expect(localStorage.getItem('lod.ledger')).toBe('terrain 32 0 0 4\nterrain 32 1 0 9\n');
    // Detects: a recorded plan staying open for a bake, which would write the same plan a second time.
    expect(screen.getByText('Generate tiles').closest('button')!.disabled).toBe(true);
    expect(screen.getByText('Dry run').closest('button')!.disabled).toBe(true);
  });

  it('refuses to record baked tiles before a plan produced commit lines', async () => {
    installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    render(<LodPanel onClose={() => {}} />);

    expect(screen.getByText('Record baked tiles').closest('button')!.disabled).toBe(true);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    expect(screen.getByText('Record baked tiles').closest('button')!.disabled).toBe(true);
  });

  // Detects: commit lines sent as the ledger, which makes the writer skip the tile that needs a bake.
  it('dry-runs the planned tiles without adopting the writer ledger', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('1 of 2 tile(s) need a rebake');

    fireEvent.click(screen.getByText('Dry run').closest('button')!);
    await screen.findByText('1 tile(s) would be written (2 unsupported object(s)), 0 unreadable world(s)');
    expect(host.Generate).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'terrain 32 1 0 9\n', false);
    const rows = screen.getByLabelText('Generate report').querySelectorAll('.lod-report-row');
    expect(Array.from(rows, row => row.textContent)).toEqual(['would-writeCommonwealthterrain320,00 B']);
    expect(localStorage.getItem('lod.ledger')).toBeNull();
    expect(screen.getByText('Generate tiles').closest('button')!.disabled).toBe(false);
  });

  // Detects: an apply that bakes a tile the plan called current, or that leaves the baked tile listed as pending.
  it('writes through the apply gate and adopts only the ledger the writer committed', async () => {
    const host = installHost();
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('1 of 2 tile(s) need a rebake');

    const apply = screen.getByText('Generate tiles').closest('button')!;
    fireEvent.click(apply);
    await screen.findByText('1 tile(s) written, 0 failed (2 unsupported object(s))');
    expect(host.Generate).toHaveBeenCalledWith('C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'terrain 32 1 0 9\n', true);
    const rows = screen.getByLabelText('Generate report').querySelectorAll('.lod-report-row');
    expect(Array.from(rows, row => row.textContent)).toEqual(['writtenCommonwealthterrain320,04096 B']);
    // Detects: a panel that shows the unsupported total without saying which reasons dropped the objects.
    const drops = screen.getByLabelText('Object drops').querySelectorAll('.lod-drop-row');
    expect(Array.from(drops, row => row.textContent)).toEqual(['unsupported_base1', 'malformed1']);
    expect(screen.getByText('2 object(s) skipped, by reason')).toBeTruthy();
    expect(localStorage.getItem('lod.ledger')).toBe('terrain 32 0 0 4\nterrain 32 1 0 9\n');
    await screen.findByText('0 of 2 tile(s) need a rebake');
    expect(apply.disabled).toBe(true);
    expect(screen.getByText('Record baked tiles').closest('button')!.disabled).toBe(true);
  });

  // Detects: a partly failed bake that records the failed tile as baked, or that locks the retry out until a replan.
  it('keeps a tile the writer failed on pending, unrecorded and ready for a retry', async () => {
    const host = installHost([1], '');
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('2 of 2 tile(s) need a rebake');

    const apply = screen.getByText('Generate tiles').closest('button')!;
    fireEvent.click(apply);
    await screen.findByText('1 tile(s) written, 1 failed (2 unsupported object(s))');
    expect(localStorage.getItem('lod.ledger')).toBe('terrain 32 0 0 4\n');
    await screen.findByText('1 of 2 tile(s) need a rebake');
    expect(apply.disabled).toBe(false);
    expect(screen.getByText('Record baked tiles').closest('button')!.disabled).toBe(true);

    fireEvent.click(apply);
    await screen.findByText('0 tile(s) written, 1 failed (2 unsupported object(s))');
    expect(host.Generate).toHaveBeenLastCalledWith(
      'C:\\Data\\Fallout4.esm',
      'C:\\Data',
      '',
      'terrain 32 0 0 4\n',
      true,
    );
  });

  it('keeps a writer failure visible instead of reporting a successful bake', async () => {
    const host = installHost();
    host.Generate = vi.fn(async () => 'Error: LOD tile write failed');
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('1 of 2 tile(s) need a rebake');

    fireEvent.click(screen.getByText('Generate tiles').closest('button')!);
    await screen.findByText('LOD tile write failed');
    expect(localStorage.getItem('lod.ledger')).toBeNull();
  });

  // Detects: a reply without a ledger being adopted as an empty one, which would forget every baked tile.
  it('refuses an apply reply that carries no ledger and keeps the stored one', async () => {
    const host = installHost();
    host.Generate = vi.fn(async () => JSON.stringify({ report: { apply: true, written: 1, tiles: [] } }));
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    localStorage.setItem('lod.ledger', 'terrain 32 1 0 9\n');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('1 of 2 tile(s) need a rebake');

    fireEvent.click(screen.getByText('Generate tiles').closest('button')!);
    await screen.findByText('LOD generate: expected a ledger');
    expect(localStorage.getItem('lod.ledger')).toBe('terrain 32 1 0 9\n');
  });

  it('shows a host failure instead of an empty plan', async () => {
    const host = installHost();
    host.Plan = vi.fn(async () => 'Error: LOD settings unreadable');
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    await screen.findByText('LOD settings unreadable');
  });

  // Detects: the object flag dropped or stuck, which previews an object tile as terrain or a terrain tile as an object.
  it('previews each tile through the writer of its own kind', async () => {
    const host = installHost();
    host.Plan = vi.fn(async () => JSON.stringify({
      plan: [
        { world: 'commonwealth', kind: 'terrain', level: 32, x: 1, y: 0, inputs: 9, rebuild: true },
        { world: 'commonwealth', kind: 'object', level: 32, x: 0, y: 0, inputs: 4, rebuild: true },
      ],
      ledger: '',
      commit: 'commonwealth|object|32|0|0 4\n',
    }));
    localStorage.setItem('lod.pluginPaths', 'C:\\Data\\Fallout4.esm');
    localStorage.setItem('lod.dataDir', 'C:\\Data');
    render(<LodPanel onClose={() => {}} />);

    fireEvent.click(screen.getByText('Load worlds'));
    await screen.findByRole('option', { name: 'Commonwealth' });
    fireEvent.click(screen.getByText('Plan rebakes'));
    fireEvent.click(await screen.findByText('object'));
    fireEvent.click(screen.getByText('Preview tile'));
    await screen.findByText(/previewed object level 32/);
    expect(host.Preview).toHaveBeenLastCalledWith(
      'C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'commonwealth', 32, 0, 0, true);

    fireEvent.click(screen.getByText('terrain'));
    fireEvent.click(screen.getByText('Preview tile'));
    await screen.findByText(/previewed terrain level 32/);
    expect(host.Preview).toHaveBeenLastCalledWith(
      'C:\\Data\\Fallout4.esm', 'C:\\Data', '', 'commonwealth', 32, 1, 0, false);
  });
});
