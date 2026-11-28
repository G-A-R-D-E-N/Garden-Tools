import type { ReactNode } from 'react';
import { Box, Clapperboard, Hash, Link2, MessageSquare, Music, Play } from 'lucide-react';
import {
  actionColumns, actionTitle, actionsUsingActor, actorName, phaseName, typeLabel,
  type SceneAction, type SceneDto, type Selection,
} from './sceneModel';

const ICONS = [MessageSquare, Box, Hash, Play, Clapperboard, Link2, Music] as const;

interface Props {
  scene: SceneDto;
  selection: Selection;
  onSelect: (selection: Selection) => void;
}

export default function SceneTimeline({ scene, selection, onSelect }: Props) {
  const phaseCount = scene.phases.length;
  if (phaseCount === 0) {
    return (
      <section className="scene-timeline" aria-label="Timeline">
        <div className="scene-note">This scene has no phases yet. Click Add phase to make the first column.</div>
      </section>
    );
  }
  const columns = `9rem repeat(${phaseCount}, minmax(7rem, 1fr))`;
  const unassigned = scene.actions.filter(action => !scene.actors.some(actor => actor.alias === action.actorAlias));

  const block = (action: SceneAction) => {
    const span = actionColumns(action, phaseCount);
    if (!span) return null;
    const Icon = ICONS[action.type] ?? Box;
    const selected = selection?.kind === 'action' && selection.index === action.index;
    const where = span.first === span.last ? `phase ${span.first + 1}` : `phases ${span.first + 1} to ${span.last + 1}`;
    return (
      <button key={action.index} type="button" className={`scene-block scene-type-${action.type}${selected ? ' is-selected' : ''}`}
        style={{ gridColumn: `${span.first + 2} / ${span.last + 3}` }}
        aria-label={`${typeLabel(action.type)}: ${actionTitle(action)}, ${where}`}
        onClick={() => onSelect({ kind: 'action', index: action.index })}>
        <Icon size={12} aria-hidden="true" />
        <span>{actionTitle(action)}</span>
      </button>
    );
  };

  const row = (key: string, label: string, head: ReactNode, actions: SceneAction[]) => (
    <div key={key} role="row" className="scene-row" aria-label={label} style={{ gridTemplateColumns: columns }}>
      <div className="scene-row-head" style={{ gridColumn: '1' }}>{head}</div>
      {actions.map(block)}
    </div>
  );

  return (
    <section className="scene-timeline" aria-label="Timeline">
      <div role="row" className="scene-row scene-row-phases" aria-label="Phases" style={{ gridTemplateColumns: columns }}>
        <div className="scene-corner" style={{ gridColumn: '1' }}>Actor</div>
        {scene.phases.map(phase => {
          const selected = selection?.kind === 'phase' && selection.index === phase.index;
          const unreadable = phase.startConditionsUnreadable || phase.completionConditionsUnreadable;
          return (
            <button key={phase.index} type="button" className={`scene-phase-head${selected ? ' is-selected' : ''}`}
              aria-label={phaseName(phase)} onClick={() => onSelect({ kind: 'phase', index: phase.index })}>
              <span className="scene-phase-title">{phaseName(phase)}</span>
              {(phase.questStageOnStart ?? 0) !== 0 && (
                <span className="scene-phase-meta">Stage {phase.questStageOnStart} on start</span>
              )}
              {unreadable && <span className="scene-badge scene-badge-warning">Conditions unreadable</span>}
            </button>
          );
        })}
      </div>
      {scene.actors.map(actor => {
        const selected = selection?.kind === 'actor' && selection.index === actor.index;
        return row(`actor-${actor.index}`, actorName(actor),
          <button type="button" className={`scene-actor-head${selected ? ' is-selected' : ''}`}
            onClick={() => onSelect({ kind: 'actor', index: actor.index })}>{actorName(actor)}</button>,
          actionsUsingActor(scene, actor.alias));
      })}
      {unassigned.length > 0 && row('unassigned', 'No actor', <span className="scene-actor-head">No actor</span>, unassigned)}
      {scene.actors.length === 0 && (
        <div className="scene-note">No actors yet. An actor is one quest alias that takes part in the scene. Click Add actor.</div>
      )}
    </section>
  );
}
