import { describe, expect, it } from 'vitest';
import type { BaseObjectItem } from './baseObjects';
import {
  buildCategoryTree, buildFolderTree, matchesCategory, modelFolders, orderedSignatures, typeIconKind, typeLabel,
  type CategoryNode,
} from './categoryTree';

function item(overrides: Partial<BaseObjectItem>): BaseObjectItem {
  return {
    formKey: '00000001:Fallout4.esm',
    hexId: '00000001',
    editorId: 'Obj',
    name: '',
    signature: 'STAT',
    typeName: 'Static',
    modelPath: '',
    winningPlugin: 'Fallout4.esm',
    deleted: false,
    hasModel: true,
    ...overrides,
  };
}

function find(nodes: CategoryNode[], id: string): CategoryNode | undefined {
  for (const node of nodes) {
    if (node.id === id) return node;
    const inner = find(node.children, id);
    if (inner) return inner;
  }
  return undefined;
}

describe('category tree type groups', () => {
  it('orders known types in spec order and unknown types last', () => {
    const order = orderedSignatures(['HAZD', 'XXXX', 'STAT', 'DOOR', 'TACT', 'WEAP']);
    expect(order).toEqual(['STAT', 'DOOR', 'WEAP', 'HAZD', 'TACT', 'XXXX']);
  });

  it('maps signatures to labels and icon kinds', () => {
    expect(typeLabel('MSTT')).toBe('Movable Statics');
    expect(typeLabel('NPC_')).toBe('NPCs');
    expect(typeLabel('BOOK')).toBe('Book');
    expect(typeLabel('ZZZZ')).toBe('ZZZZ');
    expect(typeIconKind('DOOR')).toBe('door');
    expect(typeIconKind('ZZZZ')).toBe('generic');
  });

  it('builds the library and record type sections with counts', () => {
    const tree = buildCategoryTree({
      total: 5,
      typeCounts: { STAT: 3, DOOR: 2, ZZZZ: 0 },
      scanItems: [],
      activeType: '',
      favourites: 1,
      recent: 0,
    });
    const library = find(tree, 'header:library');
    expect(library?.children.map(node => [node.label, node.count])).toEqual([
      ['All', 5],
      ['Favourites', 1],
      ['Recently used', 0],
    ]);
    const types = find(tree, 'header:types');
    expect(types?.children.map(node => [node.label, node.count])).toEqual([
      ['Statics', 3],
      ['Doors', 2],
    ]);
    expect(find(tree, 'header:plugins')).toBeUndefined();
  });

  it('keeps the active type even when its count is zero', () => {
    const tree = buildCategoryTree({
      total: 0,
      typeCounts: {},
      scanItems: [],
      activeType: 'LIGH',
      favourites: 0,
      recent: 0,
    });
    expect(find(tree, 'type:LIGH')?.count).toBe(0);
  });
});

describe('model path folders', () => {
  it('drops the file name and a leading meshes segment', () => {
    expect(modelFolders('Meshes\\Props\\Chair\\chair01.nif')).toEqual(['Props', 'Chair']);
    expect(modelFolders('Props/Chair/chair01.nif')).toEqual(['Props', 'Chair']);
    expect(modelFolders('chair.nif')).toEqual([]);
    expect(modelFolders('')).toEqual([]);
  });

  it('builds a case-insensitive folder tree with counts', () => {
    const roots = buildFolderTree([
      'Props\\Chair\\a.nif',
      'props\\chair\\b.nif',
      'Props\\Table\\c.nif',
      'Architecture\\Wall.nif',
    ]);
    expect(roots.map(root => [root.name, root.count])).toEqual([
      ['Architecture', 1],
      ['Props', 3],
    ]);
    const props = roots[1];
    expect(props.children.map(child => [child.name, child.count])).toEqual([
      ['Chair', 2],
      ['Table', 1],
    ]);
    expect(props.children[0].path).toEqual(['Props', 'Chair']);
  });

  it('nests folder categories under the active type and filters by folder', () => {
    const scan = [
      item({ formKey: 'a', modelPath: 'Meshes\\Props\\Chair\\a.nif' }),
      item({ formKey: 'b', modelPath: 'Meshes\\Props\\b.nif' }),
    ];
    const tree = buildCategoryTree({
      total: 2,
      typeCounts: { STAT: 2 },
      scanItems: scan,
      activeType: 'STAT',
      favourites: 0,
      recent: 0,
    });
    const props = find(tree, 'folder:STAT:props');
    expect(props?.count).toBe(2);
    expect(props?.children[0].selection).toEqual({ kind: 'folder', signature: 'STAT', folder: ['Props', 'Chair'] });
    const selection = props?.selection ?? null;
    expect(selection).not.toBeNull();
    if (selection) {
      expect(matchesCategory(scan[0], selection)).toBe(true);
      expect(matchesCategory(scan[1], { kind: 'folder', signature: 'STAT', folder: ['Props', 'Chair'] })).toBe(false);
    }
  });
});

describe('plugin grouping and matching', () => {
  it('groups by winning plugin with counts, skipping empty plugin names', () => {
    const tree = buildCategoryTree({
      total: 3,
      typeCounts: { STAT: 3 },
      scanItems: [
        item({ winningPlugin: 'Zed.esp' }),
        item({ winningPlugin: 'alpha.esp' }),
        item({ winningPlugin: 'ALPHA.esp' }),
        item({ winningPlugin: '' }),
      ],
      activeType: '',
      favourites: 0,
      recent: 0,
    });
    const plugins = find(tree, 'header:plugins');
    expect(plugins?.children.map(node => [node.label, node.count])).toEqual([
      ['alpha.esp', 2],
      ['Zed.esp', 1],
    ]);
    const alpha = plugins?.children[0].selection ?? null;
    expect(alpha && matchesCategory(item({ winningPlugin: 'Alpha.ESP' }), alpha)).toBe(true);
  });
});
