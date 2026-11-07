import { describe, expect, it } from 'vitest';
import {
  DEFAULT_BROWSER_QUERY, buildBaseObjectQuery, parseBaseObjectListing, parsePlaceResult, placementsJson,
  zeroTransform, type BrowserQueryState,
} from './baseObjects';

const state: BrowserQueryState = {
  ...DEFAULT_BROWSER_QUERY,
  text: '  sword ',
  type: 'WEAP',
  scopes: ['editorId'],
  match: 'regex',
  hideDeleted: true,
  sortBy: 'hexId',
  sortDir: 'desc',
};

describe('base object query builder', () => {
  it('sends only the parts of the query the user set', () => {
    expect(buildBaseObjectQuery(state, 200, 500)).toEqual({
      query: 'sword',
      type: 'WEAP',
      scopes: ['editorId'],
      match: 'regex',
      hideDeleted: true,
      sortBy: 'hexId',
      sortDir: 'desc',
      offset: 200,
      limit: 200,
    });
  });

  it('omits the text, type and flags when they are empty', () => {
    const query = buildBaseObjectQuery(DEFAULT_BROWSER_QUERY, 0, 100);
    expect(query).not.toHaveProperty('query');
    expect(query).not.toHaveProperty('type');
    expect(query).not.toHaveProperty('hideDeleted');
    expect(query).not.toHaveProperty('placeableWithModel');
    expect(query.scopes).toEqual(['editorId', 'name', 'hexId']);
  });

  it('falls back to the default scopes when none are chosen', () => {
    expect(buildBaseObjectQuery({ ...state, scopes: [] }, 0, 10).scopes).toEqual(['editorId', 'name', 'hexId']);
  });

  it('keeps the limit between one and the native maximum and the offset non-negative', () => {
    expect(buildBaseObjectQuery(state, -5, 0)).toMatchObject({ offset: 0, limit: 1 });
    expect(buildBaseObjectQuery(state, 0, 9999)).toMatchObject({ limit: 200 });
  });
});

describe('ListBaseObjects reply parsing', () => {
  it('reads the native items, type counts and paging fields', () => {
    const text = JSON.stringify({
      total: 2,
      offset: 0,
      limit: 100,
      sortBy: 'hexId',
      sortDir: 'desc',
      typeCounts: { WEAP: 2, MISC: 0 },
      items: [{
        formKey: '0000AB:Fallout4.esm',
        hexId: '0000AB',
        editorId: 'Sword01',
        name: 'Sword',
        signature: 'WEAP',
        typeName: 'Weapon',
        modelPath: 'Weapons/Sword.nif',
        winningPlugin: 'Fallout4.esm',
        deleted: false,
        hasModel: true,
      }],
    });
    const parsed = parseBaseObjectListing(text);
    expect(parsed.ok).toBe(true);
    if (!parsed.ok) return;
    expect(parsed.listing.total).toBe(2);
    expect(parsed.listing.sortBy).toBe('hexId');
    expect(parsed.listing.sortDir).toBe('desc');
    expect(parsed.listing.typeCounts).toEqual({ WEAP: 2, MISC: 0 });
    expect(parsed.listing.items[0]).toMatchObject({ hexId: '0000AB', editorId: 'Sword01', hasModel: true });
  });

  it('surfaces the native error message', () => {
    expect(parseBaseObjectListing('{"error":"Not a placeable base type: XXXX"}')).toEqual({
      ok: false,
      error: 'Not a placeable base type: XXXX',
    });
  });

  it('refuses text that is not a JSON object', () => {
    expect(parseBaseObjectListing('not json').ok).toBe(false);
    expect(parseBaseObjectListing('[1,2]').ok).toBe(false);
  });
});

describe('PlaceReference reply parsing', () => {
  it('reads the created keys and the references they produced', () => {
    const parsed = parsePlaceResult(JSON.stringify({
      plugin: 'Patch.esp',
      cellFormKey: '000ABC:Fallout4.esm',
      created: ['000ABD:Patch.esp'],
      references: [{ formKey: '000ABD:Patch.esp' }],
    }));
    expect(parsed).toEqual({
      ok: true,
      reply: {
        plugin: 'Patch.esp',
        cellFormKey: '000ABC:Fallout4.esm',
        created: ['000ABD:Patch.esp'],
        references: [{ formKey: '000ABD:Patch.esp' }],
      },
    });
  });

  it('reports a native error without a created list', () => {
    expect(parsePlaceResult('{"error":"PlacedCellNotFound"}')).toEqual({ ok: false, error: 'PlacedCellNotFound' });
  });
});

describe('placement payload', () => {
  it('serialises items with their base key and transform', () => {
    const json = placementsJson([{ baseFormKey: '0000AB:Fallout4.esm', transform: { ...zeroTransform(), x: 64 } }]);
    expect(JSON.parse(json)).toEqual([
      { baseFormKey: '0000AB:Fallout4.esm', transform: { x: 64, y: 0, z: 0, rx: 0, ry: 0, rz: 0 } },
    ]);
  });
});
