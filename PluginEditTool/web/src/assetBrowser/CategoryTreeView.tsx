import { useState } from 'react';
import { isExpandedByDefault, sameSelection, typeIconKind, type CategoryNode, type CategorySelection } from './categoryTree';
import TypeIcon from './TypeIcon';

export interface CategoryTreeProps {
  nodes: CategoryNode[];
  selection: CategorySelection | null;
  onSelect: (selection: CategorySelection) => void;
}

const INDENT = 12;
const BASE_PADDING = 6;

export default function CategoryTree({ nodes, selection, onSelect }: CategoryTreeProps) {
  const [toggled, setToggled] = useState<Record<string, boolean>>({});

  const isOpen = (node: CategoryNode): boolean => toggled[node.id] ?? isExpandedByDefault(node);

  const toggle = (node: CategoryNode) => {
    setToggled(prev => ({ ...prev, [node.id]: !isOpen(node) }));
  };

  const renderNode = (node: CategoryNode, depth: number) => {
    const hasChildren = node.children.length > 0;
    const open = hasChildren && isOpen(node);
    const isHeader = node.selection === null;
    const active = node.selection !== null && sameSelection(node.selection, selection);
    const icon = node.selection?.kind === 'type' ? typeIconKind(node.selection.signature) : null;
    return (
      <li
        key={node.id}
        role="treeitem"
        aria-level={depth + 1}
        aria-expanded={hasChildren ? open : undefined}
        aria-selected={isHeader ? undefined : active}
        className={isHeader ? 'ab-tree-section' : undefined}
      >
        <div className="ab-tree-row" style={{ paddingLeft: BASE_PADDING + depth * INDENT }}>
          {hasChildren ? (
            <button
              type="button"
              className="ab-tree-twist"
              aria-label={`${open ? 'Collapse' : 'Expand'} ${node.label}`}
              onClick={() => toggle(node)}
            >
              {open ? '\u25BE' : '\u25B8'}
            </button>
          ) : <span className="ab-tree-twist" aria-hidden="true" />}
          {isHeader ? (
            <span className="ab-tree-head">{node.label}</span>
          ) : (
            <button
              type="button"
              className={`ab-tree-label${active ? ' active' : ''}`}
              aria-pressed={active}
              onClick={() => { if (node.selection) onSelect(node.selection); }}
            >
              {icon && <TypeIcon kind={icon} size={13} />}
              <span className="ab-tree-name">{node.label}</span>
              {node.count !== null && <span className="ab-tree-count">{node.count}</span>}
            </button>
          )}
        </div>
        {open && (
          <ul role="group" className="ab-tree-group">
            {node.children.map(child => renderNode(child, depth + 1))}
          </ul>
        )}
      </li>
    );
  };

  return (
    <ul role="tree" aria-label="Categories" className="ab-tree">
      {nodes.map(node => renderNode(node, 0))}
    </ul>
  );
}
