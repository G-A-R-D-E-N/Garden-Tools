import { ChevronRight, ChevronDown, FileBox, Star, X } from 'lucide-react';
import type { Favourite } from '../favourites';

export default function FavouritesList({ favourites, open, onToggle, onOpen, onRemove }: {
  favourites: Favourite[];
  open: boolean;
  onToggle: () => void;
  onOpen: (formKey: string, plugin: string) => void;
  onRemove: (formKey: string) => void;
}) {
  return (
    <div className="sidebar-favourites">
      <button
        className="sidebar-fav-header"
        onClick={onToggle}
        title={open ? 'Collapse favourites' : 'Expand favourites'}
      >
        {open ? <ChevronDown size={13} /> : <ChevronRight size={13} />}
        <Star size={12} className="sidebar-fav-star" />
        <span>FAVOURITES</span>
        <span className="sidebar-fav-count">{favourites.length}</span>
      </button>
      {open && favourites.map(f => (
        <div
          key={f.formKey}
          className="tree-row is-record sidebar-fav-row"
          onClick={() => onOpen(f.formKey, f.plugin)}
          title={`${f.formKey}${f.plugin ? ' - ' + f.plugin : ''}`}
        >
          <div className="tree-icon-container"><div style={{ width: 14 }} /></div>
          <div className="tree-icon"><FileBox size={14} color="#569CD6" /></div>
          <span className="tree-label">{f.label}</span>
          <button
            className="sidebar-fav-remove"
            title="Remove from favourites"
            onClick={e => { e.stopPropagation(); onRemove(f.formKey); }}
          >
            <X size={11} />
          </button>
        </div>
      ))}
    </div>
  );
}
