import { useEffect, useRef, useState } from 'react';
import { ChevronRight, ChevronDown, Database, FileBox } from 'lucide-react';
import type { AppInterop } from '../backend';
import { errorText } from '../errors';

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
  const [seenNode, setSeenNode] = useState(node);
  const requests = useRef(0);

  if (seenNode !== node) {
    setSeenNode(node);
    if (!expanded) setChildren([]);
    else if (node.HasChildren) {
      setLoading(true);
      setLoadError('');
    }
  }

  useEffect(() => {
    if (!expanded || !node.HasChildren) return;
    const mine = ++requests.current;
    const current = () => mine === requests.current;
    (backend ? backend.GetChildren(path) : Promise.reject(new Error('Host bridge is not available')))
      .then(json => {
        if (!current()) return;
        setChildren(JSON.parse(json));
        setLoadError('');
      })
      .catch(err => { if (current()) setLoadError(errorText(err)); })
      .finally(() => { if (current()) setLoading(false); });
  }, [node, expanded, backend, path]);

  const shown = node.HasChildren ? children : [];

  const toggle = () => {
    if (!expanded && node.HasChildren) {
      setLoading(true);
      setLoadError('');
    } else if (expanded) {
      requests.current++;
      setLoading(false);
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
        {node.HasChildren && loading && <span className="tree-loading">...</span>}
        {node.HasChildren && !loading && loadError && (
          <span className="tree-loading" style={{ color: '#f44747' }} title={loadError}>failed</span>
        )}
      </div>
      {expanded && (
        <div className="tree-children">
          {shown.map((c, i) => (
            <ExplorerTree key={`${c.Key}:${i}`} path={`${path}\\${c.Key}`} node={c} backend={backend} onOpenRecord={onOpenRecord} />
          ))}
        </div>
      )}
    </div>
  );
}
