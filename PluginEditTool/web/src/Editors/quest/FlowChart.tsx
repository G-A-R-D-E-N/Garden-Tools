import { addStageAfter, nextStageNumber } from './flowChartEdits';
import { buildFlowChart } from './flowChartModel';
import FlowChartPanel from './FlowChartPanel';
import type { QuestObjective, QuestStage, StageEdit, StageSelection } from './questModel';
import './FlowChart.css';

interface Props {
  stages: QuestStage[];
  objectives: QuestObjective[];
  selected: StageSelection | null;
  onSelect: (index: number | null) => void;
  onEdit: (edit: StageEdit, label: string) => void;
}

export default function FlowChart({ stages, objectives, selected, onSelect, onEdit }: Props) {
  const chart = buildFlowChart(stages, objectives);
  const current = stages.find(stage => stage.index === selected?.index);
  const addNext = (after: number) => {
    const added: { index?: number } = {};
    onEdit(latest => {
      added.index = nextStageNumber(latest, after);
      return addStageAfter(latest, after);
    }, `Add stage after ${after}`);
    if (added.index !== undefined) onSelect(added.index);
  };
  return (
    <section className="quest-pane quest-flow" aria-labelledby="quest-flow-title">
      <header className="quest-pane-header">
        <h4 id="quest-flow-title">Flow chart</h4>
      </header>
      <p className="quest-note">
        Stages run left to right by number. The arrows are worked out from the stage data, so they cannot be
        edited here. Click a stage to open its details beside the chart.
      </p>
      {stages.length === 0 ? (
        <div className="editor-empty">No stages yet. Use Add stage in the Stages pane.</div>
      ) : (
        <div className="quest-flow-body">
          <div className="quest-flow-scroll">
            <div className="quest-flow-canvas" style={{ width: chart.width, height: chart.height }}>
              <svg className="quest-flow-arrows" width={chart.width} height={chart.height} aria-hidden="true">
                <defs>
                  <marker id="quest-flow-head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6"
                    orient="auto">
                    <path className="quest-flow-head" d="M0,0 L10,5 L0,10 z" />
                  </marker>
                </defs>
                {chart.arrows.map((arrow, at) => (
                  <line key={`${at}:${arrow.id}`} className={`quest-flow-arrow quest-flow-arrow-${arrow.kind}`}
                    x1={arrow.x1} y1={arrow.y1} x2={arrow.x2} y2={arrow.y2} markerEnd="url(#quest-flow-head)">
                    <title>{arrow.title}</title>
                  </line>
                ))}
              </svg>
              {chart.boxes.map((box, at) => box.kind === 'stage' ? (
                <div key={`${at}:${box.id}`} className={`quest-flow-box quest-flow-state-${box.state}`}
                  style={{ left: box.x, top: box.y, width: box.width, height: box.height }}>
                  <button type="button" className="quest-flow-select" aria-pressed={selected?.index === box.stageIndex}
                    onClick={() => onSelect(box.stageIndex)}>
                    <strong>Stage {box.stageIndex}</strong>
                    {box.text && <span className="quest-flow-text">{box.text}</span>}
                    <span className="quest-flow-badges">
                      {box.badges.map(badge => <span key={badge} className="quest-flow-badge">{badge}</span>)}
                    </span>
                  </button>
                  <button type="button" className="sidebar-action-btn quest-flow-add"
                    onClick={() => addNext(box.stageIndex)}>+ next step</button>
                </div>
              ) : (
                <div key={`${at}:${box.id}`} className="quest-flow-end"
                  style={{ left: box.x, top: box.y, width: box.width, height: box.height }}>
                  {box.text}
                </div>
              ))}
            </div>
          </div>
          {current && (
            <FlowChartPanel key={current.index} stage={current}
              hasObjective={objectives.some(objective => objective.index === current.index)}
              onEdit={onEdit} onSelect={onSelect} />
          )}
        </div>
      )}
      <p className="quest-note quest-flow-legend">
        <span className="quest-flow-legend-problem">Red border</span>: no journal text and no objective with this
        number. <span className="quest-flow-legend-ending">Blue border</span>: completes or fails the quest. Grey: fine.
      </p>
    </section>
  );
}
