import { expect, it, vi } from 'vitest';
import type { Backend } from '../../backend';
import { writeAliases } from './questBackend';
import { aliasesJson, parseAliases } from './questModel';

it('Detects: a collection alias row that carries entries parses to its entries, so the editor shows the real entry count', () => {
  const [alias] = parseAliases({
    aliases: [{ kind: 'collection', entries: [{ id: 4, maxInitialFillCount: 2 }, { id: 5 }] }],
  });
  expect(alias.kind).toBe('collection');
  expect(alias.id).toBe(4);
  expect(alias.collectionEntries).toEqual([
    { id: 4, maxInitialFillCount: 2 },
    { id: 5, maxInitialFillCount: null },
  ]);
});

it('Detects: a collection alias row that still uses the old collectionEntries key parses to its entries', () => {
  const [alias] = parseAliases({
    aliases: [{ kind: 'collection', id: 7, collectionEntries: [{ id: 7, maxInitialFillCount: 1 }] }],
  });
  expect(alias.id).toBe(7);
  expect(alias.collectionEntries).toEqual([{ id: 7, maxInitialFillCount: 1 }]);
});

it('Detects: aliasesJson and writeAliases send entries for a collection alias and no id, flags or fill keys', async () => {
  const aliases = parseAliases({
    aliases: [{ kind: 'collection', entries: [{ id: 4, maxInitialFillCount: 2 }, { id: 5 }] }],
  });
  const expected = [{
    kind: 'collection',
    entries: [{ id: 4, maxInitialFillCount: 2 }, { id: 5 }],
    name: null,
  }];
  expect(JSON.parse(aliasesJson(aliases))).toEqual(expected);

  const SetQuestAliases = vi.fn<(...args: string[]) => Promise<string>>(async () => 'ok');
  await writeAliases({ SetQuestAliases } as unknown as Backend, 'Patch.esp', '000800:Patch.esp', aliases);
  expect(SetQuestAliases).toHaveBeenCalledWith('Patch.esp', '000800:Patch.esp', JSON.stringify(expected));
});

it('Detects: a reference alias read with the getter-only extra keys sends only kind, id, flags and name, so the native setter can keep the rest', () => {
  const aliases = parseAliases({
    aliases: [{
      kind: 'reference',
      id: 0,
      name: 'Courier',
      flags: 0,
      keywords: ['000100:Patch.esp'],
      items: [{ item: '000200:Patch.esp', count: 1 }],
      conditions: [],
      forceIntoAlias: null,
      closestToAlias: null,
      uniqueActor: '000900:Patch.esp',
    }],
  });
  expect(JSON.parse(aliasesJson(aliases))).toEqual([{ kind: 'reference', id: 0, flags: 0, name: 'Courier' }]);
});

it('Detects: clearing a location alias flags and name sends null, so the native setter clears them instead of keeping the old values', () => {
  const aliases = parseAliases({ aliases: [{ kind: 'location', id: 3, name: '', flags: null }] });
  expect(JSON.parse(aliasesJson(aliases))).toEqual([{ kind: 'location', id: 3, flags: null, name: null }]);
});
