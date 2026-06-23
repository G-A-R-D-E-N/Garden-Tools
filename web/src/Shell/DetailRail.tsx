import { Info } from 'lucide-react';
import type { ConflictMatrix } from '../backend';
import OverriddenBy from '../Workspace/OverriddenBy';
import './DetailRail.css';

interface DetailRailProps {
  matrix: ConflictMatrix | null;
}

// Phase 1 placeholder detail rail. Phase 5 fills in donut/quick-actions/contained-in.
export default function DetailRail({ matrix }: DetailRailProps) {
  return (
    <aside className="detail-rail">
      <div className="rail-section">
        <div className="rail-head">RECORD DETAILS</div>
        {matrix ? (
          <div className="rail-fields">
            <Field label="Editor ID" value={matrix.EditorID || '-'} />
            <Field label="Form Key" value={matrix.FormKey} mono />
            <Field label="Type" value={matrix.Type || '-'} />
            <Field label="Winner" value={matrix.Winner || '-'} />
          </div>
        ) : (
          <div className="rail-empty">
            <Info size={20} />
            <span>Open a record to see its details.</span>
          </div>
        )}
      </div>
      {/* Record-level summary so the section is usable now. Phase 4 will render
          OverriddenBy per conflict-group card instead of this single rail rollup.
          Plugins[0] is the base master, so slice(1) is everything overriding it. */}
      {matrix && matrix.Plugins.length > 1 && (
        <OverriddenBy plugins={matrix.Plugins.slice(1)} />
      )}
    </aside>
  );
}

function Field({ label, value, mono }: { label: string; value: string; mono?: boolean }) {
  return (
    <div className="rail-field">
      <span className="rf-label">{label}</span>
      <span className={`rf-value ${mono ? 'mono' : ''}`} title={value}>{value}</span>
    </div>
  );
}
