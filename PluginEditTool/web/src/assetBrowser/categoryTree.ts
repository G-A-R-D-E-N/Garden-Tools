import type { BaseObjectItem } from './baseObjects';
import { PLACEABLE_TYPES } from './placeableTypes';

export type IconKind = 'static' | 'door' | 'light' | 'weapon' | 'armor' | 'ammo' | 'container' | 'npc' | 'flora'
  | 'hazard' | 'terminal' | 'furniture' | 'activator' | 'misc' | 'generic';

export type CategorySelection =
  | { kind: 'all' }
  | { kind: 'type'; signature: string }
  | { kind: 'folder'; signature: string; folder: string[] }
  | { kind: 'plugin'; signature: string; plugin: string }
  | { kind: 'favourites' }
  | { kind: 'recent' };

export interface CategoryNode {
  id: string;
  label: string;
  count: number | null;
  selection: CategorySelection | null;
  children: CategoryNode[];
}

export interface FolderNode {
  name: string;
  path: string[];
  count: number;
  children: FolderNode[];
}

export interface CategoryInput {
  total: number;
  typeCounts: Record<string, number>;
  scanItems: readonly BaseObjectItem[];
  activeType: string;
  favourites: number;
  recent: number;
}

const TYPE_PRESENTATION: Record<string, { label: string; icon: IconKind }> = {
  STAT: { label: 'Statics', icon: 'static' },
  MSTT: { label: 'Movable Statics', icon: 'static' },
  FURN: { label: 'Furniture', icon: 'furniture' },
  DOOR: { label: 'Doors', icon: 'door' },
  ACTI: { label: 'Activators', icon: 'activator' },
  CONT: { label: 'Containers', icon: 'container' },
  LIGH: { label: 'Lights', icon: 'light' },
  WEAP: { label: 'Weapons', icon: 'weapon' },
  ARMO: { label: 'Armor', icon: 'armor' },
  AMMO: { label: 'Ammo', icon: 'ammo' },
  MISC: { label: 'Misc', icon: 'misc' },
  TERM: { label: 'Terminals', icon: 'terminal' },
  NPC_: { label: 'NPCs', icon: 'npc' },
  FLOR: { label: 'Flora', icon: 'flora' },
  HAZD: { label: 'Hazards', icon: 'hazard' },
  TACT: { label: 'Talking Activators', icon: 'activator' },
};

const TYPE_KEYS = Object.keys(TYPE_PRESENTATION);
const FOLDER_EXPANDED_DEPTH = 2;

export function typeLabel(signature: string): string {
  return TYPE_PRESENTATION[signature]?.label ?? PLACEABLE_TYPES.find(t => t.signature === signature)?.name ?? signature;
}

export function typeIconKind(signature: string): IconKind {
  return TYPE_PRESENTATION[signature]?.icon ?? 'generic';
}

export function orderedSignatures(signatures: readonly string[]): string[] {
  const rank = (signature: string): number => {
    const preset = TYPE_KEYS.indexOf(signature);
    if (preset >= 0) return preset;
    const placeable = PLACEABLE_TYPES.findIndex(t => t.signature === signature);
    return TYPE_KEYS.length + (placeable >= 0 ? placeable : PLACEABLE_TYPES.length);
  };
  return [...signatures].sort((a, b) => rank(a) - rank(b) || a.localeCompare(b));
}

export function modelFolders(modelPath: string): string[] {
  const parts = modelPath.trim().split(/[\\/]+/);
  const dirs = parts.slice(0, -1).filter(part => part !== '');
  if (dirs[0]?.toLowerCase() === 'meshes') dirs.shift();
  return dirs;
}

export function buildFolderTree(modelPaths: readonly string[]): FolderNode[] {
  const nodes = new Map<string, FolderNode>();
  const roots: FolderNode[] = [];
  for (const modelPath of modelPaths) {
    let siblings = roots;
    const prefix: string[] = [];
    for (const segment of modelFolders(modelPath)) {
      prefix.push(segment);
      const key = prefix.map(part => part.toLowerCase()).join('\\');
      let node = nodes.get(key);
      if (!node) {
        node = { name: segment, path: [...prefix], count: 0, children: [] };
        nodes.set(key, node);
        siblings.push(node);
      }
      node.count++;
      siblings = node.children;
    }
  }
  sortFolders(roots);
  return roots;
}

function sortFolders(nodes: FolderNode[]): void {
  nodes.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }));
  for (const node of nodes) sortFolders(node.children);
}

export function matchesCategory(item: BaseObjectItem, selection: CategorySelection): boolean {
  switch (selection.kind) {
    case 'folder': return startsWithFolder(modelFolders(item.modelPath), selection.folder);
    case 'plugin': return item.winningPlugin.toLowerCase() === selection.plugin.toLowerCase();
    default: return true;
  }
}

function startsWithFolder(dirs: readonly string[], folder: readonly string[]): boolean {
  if (folder.length > dirs.length) return false;
  return folder.every((segment, i) => segment.toLowerCase() === dirs[i].toLowerCase());
}

export function sameSelection(a: CategorySelection | null, b: CategorySelection | null): boolean {
  return JSON.stringify(a) === JSON.stringify(b);
}

export function isExpandedByDefault(node: CategoryNode): boolean {
  if (node.selection?.kind !== 'folder') return true;
  return node.selection.folder.length <= FOLDER_EXPANDED_DEPTH;
}

export function buildCategoryTree(input: CategoryInput): CategoryNode[] {
  const library: CategoryNode[] = [
    { id: 'all', label: 'All', count: input.total, selection: { kind: 'all' }, children: [] },
    { id: 'favourites', label: 'Favourites', count: input.favourites, selection: { kind: 'favourites' }, children: [] },
    { id: 'recent', label: 'Recently used', count: input.recent, selection: { kind: 'recent' }, children: [] },
  ];
  const signatures = new Set(Object.keys(input.typeCounts));
  if (input.activeType !== '') signatures.add(input.activeType);
  const types: CategoryNode[] = orderedSignatures([...signatures])
    .filter(signature => (input.typeCounts[signature] ?? 0) > 0 || signature === input.activeType)
    .map(signature => ({
      id: `type:${signature}`,
      label: typeLabel(signature),
      count: input.typeCounts[signature] ?? 0,
      selection: { kind: 'type', signature },
      children: [],
    }));
  const activeNode = input.activeType === '' ? library[0] : types.find(node => node.id === `type:${input.activeType}`);
  if (activeNode) {
    activeNode.children = buildFolderTree(input.scanItems.map(item => item.modelPath))
      .map(node => toCategoryNode(node, input.activeType));
  }
  const plugins = pluginNodes(input.scanItems, input.activeType);
  const sections: CategoryNode[] = [
    { id: 'header:library', label: 'Library', count: null, selection: null, children: library },
    { id: 'header:types', label: 'Record types', count: null, selection: null, children: types },
  ];
  if (plugins.length > 0) sections.push({ id: 'header:plugins', label: 'Plugins', count: null, selection: null, children: plugins });
  return sections;
}

function toCategoryNode(node: FolderNode, signature: string): CategoryNode {
  return {
    id: `folder:${signature}:${node.path.join('\\').toLowerCase()}`,
    label: node.name,
    count: node.count,
    selection: { kind: 'folder', signature, folder: node.path },
    children: node.children.map(child => toCategoryNode(child, signature)),
  };
}

function pluginNodes(items: readonly BaseObjectItem[], activeType: string): CategoryNode[] {
  const counts = new Map<string, { name: string; count: number }>();
  for (const item of items) {
    if (item.winningPlugin === '') continue;
    const key = item.winningPlugin.toLowerCase();
    const entry = counts.get(key) ?? { name: item.winningPlugin, count: 0 };
    entry.count++;
    counts.set(key, entry);
  }
  return [...counts.values()]
    .sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }))
    .map(entry => ({
      id: `plugin:${entry.name.toLowerCase()}`,
      label: entry.name,
      count: entry.count,
      selection: { kind: 'plugin', signature: activeType, plugin: entry.name },
      children: [],
    }));
}
