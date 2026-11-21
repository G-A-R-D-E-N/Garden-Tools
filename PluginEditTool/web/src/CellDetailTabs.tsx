import { DETAIL_TABS, type DetailTab } from './cellDetailTab';

export default function CellDetailTabs(
  { active, onSelect }: { active: DetailTab; onSelect: (tab: DetailTab) => void }
) {
  return (
    <div className="cell-detail-tabs" role="tablist" aria-label="Cell Viewer sections">
      {DETAIL_TABS.map(t => (
        <button
          key={t.id}
          role="tab"
          aria-selected={active === t.id}
          className={`cell-detail-tab${active === t.id ? ' active' : ''}`}
          onClick={() => onSelect(t.id)}
        >{t.label}</button>
      ))}
    </div>
  );
}
