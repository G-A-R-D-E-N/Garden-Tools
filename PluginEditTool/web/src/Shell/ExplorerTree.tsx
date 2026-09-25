import { useState } from 'react';
import { ChevronRight, ChevronDown, Database, FileBox } from 'lucide-react';
import type { AppInterop } from '../backend';
import { errorMessage } from '../errors';

export interface RecordNode {
  Key: string;
  ConflictStatus: number;
  IsRecordNode: boolean;
  HasChildren: boolean;
  FilePath: string;
}

export default function ExplorerTree({ path, node, backend, onOpenRecord }: { path: string, node: RecordNode, backend: AppInterop | null, onOpenRecord: (path: string, node: RecordNode) => void }) {
  const [expanded, setExpanded] = useState(false);
  const [children, setChildren] = useState<RecordNode[]>([]);
  const [loading, setLoading] = useState(false);
  const [loadError, setLoadError] = useState('');

  const toggle = async () => {
    if (!expanded) {
      if (node.HasChildren && children.length === 0) {
        setLoading(true);


        try {
          if (!backend) throw new Error('Host bridge is not available');
          const json = await backend.GetChildren(path);
          setChildren(JSON.parse(json));
        } catch (err) {
          setLoadError(errorMessage(err) ?? String(err));
        } finally {
          setLoading(false);
        }
      }
    }
    setExpanded(!expanded);
  };

  return (
    <div className="tree-node">
      <div
        className={`tree-row ${node.IsRecordNode ? 'is-record' : ''}`}
        onClick={node.IsRecordNode ? () => onOpenRecord(path, node) : toggle}
      >
        <div className="tree-icon-container">
          {node.HasChildren ? (
            expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />
          ) : <div style={{width: 14}}/>}
        </div>
        <div className="tree-icon">
          {node.IsRecordNode ? <FileBox size={14} color="#569CD6"/> : <Database size={14} color="#DCDCAA" />}
        </div>
        <span
          className="tree-label"
          style={node.ConflictStatus === 1 ? { color: '#c79a5e' }
            : node.ConflictStatus === 2 ? { color: '#b07a7a' }
            : undefined}
        >{node.Key}</span>
        {loading && <span className="tree-loading">...</span>}
        {!loading && loadError && (
          <span className="tree-loading" style={{ color: '#f44747' }} title={loadError}>failed</span>
        )}
      </div>
      {expanded && (
        <div className="tree-children">
          {children.map((c, i) => (
            <ExplorerTree key={`${c.Key}:${i}`} path={`${path}\\${c.Key}`} node={c} backend={backend} onOpenRecord={onOpenRecord} />
          ))}
        </div>
      )}
    </div>
  );
}
