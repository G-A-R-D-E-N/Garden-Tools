import { useCallback, useEffect, useState } from 'react';
import { Search, Undo2 } from 'lucide-react';
import { getBackend } from '../../backend';
import ConditionsEditor from '../../ConditionsEditor';
import RecordPicker from '../../RecordPicker';
import { errorText } from '../../errors';
import { friendlyError, type FriendlyMessage } from '../friendlyErrors';
import { useUndoStack } from '../useUndoStack';
import ActionForm from './ActionForm';
import ActorForm from './ActorForm';
import AddBar from './AddBar';
import PhaseForm from './PhaseForm';
import SceneSettingsForm from './SceneSettingsForm';
import SceneTimeline from './SceneTimeline';
import { Notice } from './sceneFormParts';
import { readConditions, sceneChanges, writeConditions, type Run } from './sceneChanges';
import { parseScene, typeLabel, type ConditionsTarget, type SceneDto, type Selection } from './sceneModel';
import './scene.css';

interface Props {
  sceneKey: string | null;
  plugin: string;
  onPickScene: (formKey: string, plugin: string) => void;
}

export default function SceneWorkspace({ sceneKey, plugin, onPickScene }: Props) {
  const b = getBackend() ?? null;
  const { push, undo, redo, canUndo, canRedo, scopeRef, onKeyDown } = useUndoStack();
  const [scene, setScene] = useState<SceneDto | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [selection, setSelection] = useState<Selection>(null);
  const [revision, setRevision] = useState(0);
  const [failure, setFailure] = useState<FriendlyMessage | null>(null);
  const [picking, setPicking] = useState(false);
  const [conditions, setConditions] = useState<ConditionsTarget | null>(null);
  const changes = b && sceneKey ? sceneChanges(b, plugin, sceneKey) : null;

  const load = useCallback(() => {
    if (!b || !sceneKey) return Promise.resolve();
    return Promise.resolve()
      .then(() => b.GetScene(plugin, sceneKey))
      .then(raw => { setScene(parseScene(raw)); setLoadError(null); })
      .catch(e => setLoadError(errorText(e)))
      .finally(() => setRevision(current => current + 1));
  }, [b, sceneKey, plugin]);

  useEffect(() => { void load(); }, [load]);

  const replay = (step: () => Promise<void>) => () => {
    setFailure(null);
    step().catch(e => setFailure(friendlyError(errorText(e)))).finally(load);
  };

  const run: Run = (change, after) => {
    setFailure(null);
    change.apply()
      .then(() => {
        push({ label: change.label, undo: replay(change.revert), redo: replay(change.apply) });
        if (after !== undefined) setSelection(after);
      })
      .catch(e => setFailure(friendlyError(errorText(e))))
      .finally(load);
  };

  const openConditions = async (path: string, label: string) => {
    if (!b || !sceneKey) return;
    try {
      setConditions({ path, label, rows: await readConditions(b, plugin, sceneKey, path) });
    } catch (e) {
      setFailure(friendlyError(errorText(e)));
    }
  };

  const conditionsSaved = async () => {
    const target = conditions;
    setConditions(null);
    if (!b || !sceneKey || !target) return;
    try {
      const next = await readConditions(b, plugin, sceneKey, target.path);
      push({
        label: 'Change conditions',
        undo: replay(() => writeConditions(b, plugin, sceneKey, target.path, target.rows)),
        redo: replay(() => writeConditions(b, plugin, sceneKey, target.path, next)),
      });
    } catch (e) {
      setFailure(friendlyError(errorText(e)));
    }
    await load();
  };

  const phase = selection?.kind === 'phase' ? scene?.phases.find(entry => entry.index === selection.index) : undefined;
  const actor = selection?.kind === 'actor' ? scene?.actors.find(entry => entry.index === selection.index) : undefined;
  const action = selection?.kind === 'action' ? scene?.actions.find(entry => entry.index === selection.index) : undefined;

  return (
    <section ref={scopeRef} tabIndex={-1} className="scene-workspace" onKeyDown={onKeyDown}>
      <div className="scene-toolbar">
        <button type="button" className="sidebar-action-btn" onClick={() => setPicking(true)}>
          <Search size={14} /> {sceneKey ? 'Open another scene' : 'Pick a scene'}
        </button>
        <button type="button" className="sidebar-action-btn" onClick={undo} disabled={!canUndo}
          title="Undo the last change (Ctrl+Z)"><Undo2 size={14} /> Undo</button>
        <button type="button" className="sidebar-action-btn" onClick={redo} disabled={!canRedo}
          title="Redo the change (Ctrl+Y)">Redo</button>
        {scene && <span className="scene-title">{scene.editorId || scene.formKey}</span>}
      </div>
      {!b && <Notice tone="warning">The backend is not connected, so scenes cannot be read or changed.</Notice>}
      {failure && (
        <Notice tone="warning">
          {failure.text}
          {failure.details && <details><summary>Technical details</summary><pre>{failure.details}</pre></details>}
        </Notice>
      )}
      {loadError && <Notice tone="warning">This scene could not be opened. {loadError}</Notice>}
      {!sceneKey && <div className="editor-empty">Pick a scene to see its phases, actors and actions.</div>}
      {sceneKey && !scene && !loadError && <div className="scene-note">Loading the scene...</div>}
      {scene && changes && (
        <>
          {scene.structureError && <Notice tone="warning">{scene.structureError}</Notice>}
          <AddBar phaseCount={scene.phases.length}
            onAddPhase={() => run(changes.addPhase(scene.phases.length), { kind: 'phase', index: scene.phases.length })}
            onAddActor={() => run(changes.addActor(scene.actors.length), { kind: 'actor', index: scene.actors.length })}
            onAddAction={type => run(changes.addAction(scene.actions.length, type, {
              name: typeLabel(type),
              actorAlias: scene.actors[0]?.alias ?? -1,
              startPhase: 0,
              endPhase: 0,
            }), { kind: 'action', index: scene.actions.length })} />
          <SceneTimeline scene={scene} selection={selection} onSelect={setSelection} />
          {scene.actions.length === 0 && (
            <div className="scene-note">No actions yet. Pick an action type above and click Add action.</div>
          )}
          <div className="scene-details">
            {phase && (
              <PhaseForm key={`phase-${phase.index}-${revision}`} scene={scene} phase={phase} changes={changes} run={run}
                onOpenConditions={openConditions} />
            )}
            {actor && <ActorForm key={`actor-${actor.index}-${revision}`} scene={scene} actor={actor} changes={changes} run={run} />}
            {action && <ActionForm key={`action-${action.index}-${revision}`} scene={scene} action={action} changes={changes} run={run} />}
            {!phase && !actor && !action && (
              <SceneSettingsForm key={`scene-${revision}`} scene={scene} changes={changes} run={run}
                onOpenConditions={openConditions} />
            )}
          </div>
        </>
      )}
      {picking && (
        <RecordPicker title="Scene" refType="SCEN" refTypes="SCEN"
          onPick={(formKey, pickedPlugin) => { setPicking(false); onPickScene(formKey, pickedPlugin); }}
          onClose={() => setPicking(false)} />
      )}
      {conditions && b && sceneKey && (
        <ConditionsEditor plugin={plugin} record={sceneKey} path={conditions.path} label={conditions.label}
          onClose={() => setConditions(null)} onSaved={() => { void conditionsSaved(); }} />
      )}
    </section>
  );
}
