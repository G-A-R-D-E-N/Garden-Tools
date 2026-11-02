import { X } from 'lucide-react';
import type { CompareView as CompareViewData } from './useConflictBatch';
import './CompareView.css';

export default function CompareView({ view, onClose }: { view: CompareViewData; onClose: () => void }) {
  return (
    <div className="compare-view glass-panel" role="region" aria-label="Compare selected records">
      <div className="compare-view-header">
        <span>{`Comparing ${view.columns.length} records`}</span>
        <button className="compare-view-close" onClick={onClose} title="Close comparison" aria-label="Close comparison">
          <X size={14} />
        </button>
      </div>
      <div className="compare-view-scroll">
        <table>
          <thead>
            <tr>
              <th scope="col">Element</th>
              {view.columns.map(c => (
                <th scope="col" key={`${c.plugin}|${c.formKey}`}>
                  <div>{c.plugin}</div>
                  <div className="compare-view-formkey">{c.formKey}</div>
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {view.rows.map(row => (
              <tr key={row.field} className={row.differs ? 'differs' : undefined}>
                <th scope="row">{row.label}</th>
                {row.cells.map((cell, i) => (
                  <td key={i} className={cell.differs ? 'differs' : undefined}>{cell.value}</td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
