import { buildFlowChart } from './flowChartModel';
import type { QuestObjective, QuestStage } from './questModel';
import './FlowChart.css';

interface Props {
  stages: QuestStage[];
  objectives: QuestObjective[];
  selected: number | null;
  onSelect: (index: number) => void;
}

export default function FlowChart({ stages, objectives, selected, onSelect }: Props) {
  const chart = buildFlowChart(stages, objectives);
  return (
    <section className="quest-pane quest-flow" aria-labelledby="quest-flow-title">
      <header className="quest-pane-header">
        <h4 id="quest-flow-title">Flow chart</h4>
      </header>
      <p className="quest-note">
        Stages run left to right by number. The arrows are worked out from the stage data, so they cannot be
        edited here. Click a stage to open its card below.
      </p>
      {stages.length === 0 ? (
        <div className="editor-empty">There are no stages to draw yet.</div>
      ) : (
        <div className="quest-flow-canvas" style={{ width: chart.width, height: chart.height }}>
          <svg className="quest-flow-arrows" width={chart.width} height={chart.height} aria-hidden="true">
            <defs>
              <marker id="quest-flow-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6"
                orient="auto">
                <path className="quest-flow-head" d="M0,0 L10,5 L0,10 z" />
              </marker>
            </defs>
            {chart.arrows.map(arrow => (
              <line key={arrow.id} className={`quest-flow-arrow quest-flow-arrow-${arrow.kind}`}
                x1={arrow.x1} y1={arrow.y1} x2={arrow.x2} y2={arrow.y2} markerEnd="url(#quest-flow-head)">
                <title>{arrow.title}</title>
              </line>
            ))}
          </svg>
          {chart.boxes.map(box => box.kind === 'stage' ? (
            <button key={box.id} type="button" className="quest-flow-box" aria-pressed={selected === box.stageIndex}
              style={{ left: box.x, top: box.y, width: box.width, height: box.height }}
              onClick={() => onSelect(box.stageIndex)}>
              <strong>Stage {box.stageIndex}</strong>
              {box.text && <span className="quest-flow-text">{box.text}</span>}
              <span className="quest-flow-badges">
                {box.badges.map(badge => <span key={badge} className="quest-flow-badge">{badge}</span>)}
              </span>
            </button>
          ) : (
            <div key={box.id} className="quest-flow-end"
              style={{ left: box.x, top: box.y, width: box.width, height: box.height }}>
              {box.text}
            </div>
          ))}
        </div>
      )}
    </section>
  );
}
