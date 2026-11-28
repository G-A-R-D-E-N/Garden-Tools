import { describe, expect, it } from 'vitest';
import {
  STRUCTURAL_LABEL, diagReportJson, diagReportText, isStructuralOnly, parseDiagResults, summarise,
  type DiagContext, type DiagResult, type DiagStatus, type EvidenceKind,
} from './diagnosticModel';

const diag = (id: string, status: DiagStatus, evidence: EvidenceKind[] = ['real-game-data'], over: Partial<DiagResult> = {}): DiagResult => ({
  id, title: `Title ${id}`, status, evidence, left: null, right: null, diffs: [], detail: '', durationMs: 0, outputs: [], ...over,
});

const CONTEXT: DiagContext = {
  versions: { fo4ide: '1.2.3', radicleAbi: '7', commit: 'abc1234def' },
  dataDir: 'C:\\Users\\Bob\\Games\\Fallout 4\\Data',
  mo2Profile: 'Default',
  loadOrder: ['Fallout4.esm', 'Mod.esp'],
  assetRootsLowToHigh: ['D:\\mods\\base', 'D:\\mods\\top'],
  settings: { theme: 'dark', apiKey: 'key-abc-123', 'X-FO4IDE-Token': 'tok-xyz-789', cookieJar: 'sess-555' },
  gameBuild: '1.10.163',
};

describe('parseDiagResults', () => {
  it('reads an unknown, missing or non-string status as unverified (a host typo must not show as a green pass)', () => {
    const parsed = parseDiagResults([
      { id: 'a', title: 'A', status: 'passed' }, { id: 'b', title: 'B' }, { id: 'c', title: 'C', status: 42 },
      { id: 'd', title: 'D', status: 'pass', evidence: ['real-game-data'] },
    ]);
    expect(parsed.map(result => result.status)).toEqual(['unverified', 'unverified', 'unverified', 'pass']);
  });

  it('drops unknown evidence kinds (an invented kind must not show as proof)', () => {
    const parsed = parseDiagResults([{ id: 'a', title: 'A', status: 'pass', evidence: ['real-game-data', 'magic', 7] }]);
    expect(parsed).toHaveLength(1);
    expect(parsed[0].evidence).toEqual(['real-game-data']);
    expect(parseDiagResults('nope')).toEqual([]);
  });

  it('keeps a malformed row as an unverified "malformed row" result (a broken host row must not silently shrink the list)', () => {
    const parsed = parseDiagResults([{ id: 'a', title: 'A', status: 'pass', evidence: ['real-game-data'] }, 'text', null, {}, { status: 'pass' }]);
    expect(parsed).toHaveLength(5);
    for (const row of parsed.slice(1)) {
      expect(row).toMatchObject({ id: '(no id)', status: 'unverified', detail: 'malformed row' });
    }
  });

  it('trusts observed-in-game only with detail (a bare claim must not look like an in-game observation)', () => {
    const parsed = parseDiagResults([
      { id: 'a', title: 'A', status: 'pass', evidence: ['observed-in-game'], detail: '' },
      { id: 'b', title: 'B', status: 'pass', evidence: ['observed-in-game'], detail: 'Loaded the save, quest started.' },
    ]);
    expect(parsed[0].evidence).toEqual([]);
    expect(isStructuralOnly(parsed[0])).toBe(true);
    expect(parsed[1].evidence).toEqual(['observed-in-game']);
    expect(isStructuralOnly(parsed[1])).toBe(false);
  });

  it('keeps both sides and stringifies non-string values (a numeric side must not vanish from the comparison)', () => {
    const [row] = parseDiagResults([{ id: 'a', title: 'A', status: 'fail', left: { label: 'L', source: 'x.esp', value: 12 },
      right: { label: 'R', source: 'ref', value: { n: 13 } } }]);
    expect(row.left).toEqual({ label: 'L', source: 'x.esp', value: '12' });
    expect(row.right).toEqual({ label: 'R', source: 'ref', value: '{"n":13}' });
  });
});

describe('summarise', () => {
  it('never counts skip, unsupported, unverified, fail or error as passed (no "10 passed" with skipped members)', () => {
    const sum = summarise([
      diag('1', 'pass'), diag('2', 'pass', ['structural']), diag('3', 'skip'), diag('4', 'unsupported'),
      diag('5', 'unverified'), diag('6', 'fail'), diag('7', 'error'),
    ]);
    expect(sum).toEqual({ total: 7, pass: 2, fail: 1, skip: 1, unsupported: 1, unverified: 1, error: 1, structuralOnly: 1 });
  });
});

describe('diagnostic report', () => {
  const results = [
    diag('p1', 'pass', ['structural']), diag('p2', 'pass', ['real-game-data']), diag('s1', 'skip'), diag('u1', 'unsupported'),
    diag('v1', 'unverified', []), diag('f1', 'fail', ['independent-reference'], {
      left: { label: 'Plugin', source: 'Mod.esp', value: 'a' }, right: { label: 'Reference', source: 'xEdit', value: 'b' },
      diffs: ['field differs'], outputs: ['D:\\out\\f1.txt'],
    }),
  ];

  it('labels a structural-only pass and no other pass (a parser-only pass must not read as proof)', () => {
    const text = diagReportText({ context: CONTEXT, results });
    expect(text.split(STRUCTURAL_LABEL)).toHaveLength(2);
    expect(text).toContain(`[PASS] Title p1 (p1)\n  evidence: structural\n  ${STRUCTURAL_LABEL}`);
    const json = JSON.parse(diagReportJson({ context: CONTEXT, results }));
    expect(json.results.pass.map((row: { structuralOnly: boolean }) => row.structuralOnly)).toEqual([true, false]);
  });

  it('lists skipped, unsupported and unverified apart from passed (skips must not fold into the passed list or count)', () => {
    const text = diagReportText({ context: CONTEXT, results });
    const section = (name: string) => text.split('== ').find(part => part.startsWith(name)) ?? '';
    expect(section('PASSED (2)')).toContain('Title p1');
    expect(section('PASSED (2)')).not.toMatch(/Title (s1|u1|v1|f1)/);
    expect(section('SKIPPED')).toContain('Title s1');
    expect(section('UNSUPPORTED')).toContain('Title u1');
    expect(section('UNVERIFIED')).toContain('Title v1');
    expect(text).toContain('Passed: 2 (structural only: 1)');
    expect(text.indexOf('== UNVERIFIED')).toBeLessThan(text.indexOf('== PASSED'));
    const json = JSON.parse(diagReportJson({ context: CONTEXT, results }));
    const counts = Object.entries(json.results).map(([key, rows]) => [key, (rows as unknown[]).length]);
    expect(Object.fromEntries(counts)).toEqual({ fail: 1, error: 0, unverified: 1, pass: 2, skip: 1, unsupported: 1 });
  });

  it('prints the context, both sides, diffs and output paths (a report must be reproducible)', () => {
    const text = diagReportText({ context: CONTEXT, results, toolId: 'plugin.compare' });
    for (const needle of ['FO4IDE version: 1.2.3', 'Build commit: abc1234def', 'Radicle ABI: 7', '2. D:\\mods\\top', 'MO2 profile: Default', '2. Mod.esp', 'Checks run: plugin.compare',
      'left: Plugin [Mod.esp] = a', 'right: Reference [xEdit] = b', '- field differs', '- D:\\out\\f1.txt']) {
      expect(text).toContain(needle);
    }
  });

  it('says a cancelled run is partial in both forms (partial rows must not read as a complete run)', () => {
    const input = { context: CONTEXT, results: [diag('p1', 'pass')], cancelled: true };
    expect(diagReportText(input)).toContain('Run was cancelled: results are partial');
    const json = JSON.parse(diagReportJson(input));
    expect(json.cancelled).toBe(true);
    expect(json.note).toBe('Run was cancelled: results are partial');
    expect(diagReportText({ ...input, cancelled: false })).not.toContain('cancelled');
  });

  it('redacts secret settings, header and URL tokens and the user profile name in both forms (no leak when pasted)', () => {
    const leaky = [diag('x', 'error', [], {
      detail: 'X-FO4IDE-Token: tok-header-111\nAuthorization: Bearer bearer-222\nopened http://127.0.0.1:5000/#/a?t=url-token-333\n'
        + 'loaded /home/alice/mods/a.esp and C:\\Users\\Bob\\x.esp, C:\\Users\\John Smith\\AppData\\y.esp, hash '
        + 'a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5\n'
        + 'Cookie: sid=cookie-one; sessionid=cookie-two\nbasic header ok\n'
        + 'upper http://127.0.0.1:5000/#/a?T=upper-444 and file:///home/dave/x.esp and \\\\wsl$\\Ubuntu\\home\\erin\\x.esp '
        + 'and \\\\srv\\share\\Users\\frank\\x.esp\nsessionid=sid-1 passwd=pw-2 private_key: pk-3 credential=cr-4',
      diffs: ['{"password":"pw-444"}'],
      left: { label: 'L', source: '[/home/carol/z.esp]', value: 'v' },
    })];
    const input = { context: CONTEXT, results: leaky, error: 'secret=sec-555' };
    for (const out of [diagReportText(input), diagReportJson(input)]) {
      for (const leak of ['key-abc-123', 'tok-xyz-789', 'sess-555', 'tok-header-111', 'bearer-222', 'url-token-333', 'pw-444',
        'sec-555', 'Bob', 'alice', 'John', 'Smith', 'carol', 'cookie-one', 'cookie-two', 'upper-444', 'dave', 'erin', 'frank',
        'sid-1', 'pw-2', 'pk-3', 'cr-4']) {
        expect(out).not.toContain(leak);
      }
      expect(out).toContain('basic header ok');
      expect(out).toContain('%USERPROFILE%');
      expect(out).toContain('a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5c0d1e2b4a3f5');
      expect(out).toContain('theme');
    }
    expect(JSON.parse(diagReportJson(input)).context.settings.apiKey).toBe('[redacted]');
  });
});
