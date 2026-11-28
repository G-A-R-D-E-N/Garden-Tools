import { LoaderCircle } from 'lucide-react';
import { INDEXING_MESSAGE, NO_MATCH_MESSAGE } from './tableModel';

export interface ListEmptyStateProps {
  items: number;
  loading: boolean;
  failed: boolean;
}

export default function ListEmptyState({ items, loading, failed }: ListEmptyStateProps) {
  if (items > 0) return null;
  if (loading) {
    return (
      <div className="ab-empty ab-indexing" role="status">
        <LoaderCircle className="ab-spin" size={14} aria-hidden="true" />
        {INDEXING_MESSAGE}
      </div>
    );
  }
  if (failed) return null;
  return <div className="ab-empty">{NO_MATCH_MESSAGE}</div>;
}
