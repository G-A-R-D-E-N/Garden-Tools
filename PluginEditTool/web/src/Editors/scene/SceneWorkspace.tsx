import { useEffect, useState } from 'react';
import { Search, Undo2 } from 'lucide-react';
import { getBackend } from '../../backend';
import ConditionsEditor from '../../ConditionsEditor';
import RecordPicker from '../../RecordPicker';
import { errorText } from '../../errors';
import { EditorTabPanel, EditorTabs } from '../EditorTabs';
import { friendlyError, type FriendlyMessage } from '../friendlyErrors';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import { useReadOnly } from '../readOnlyContext';
import { useUndoStack } from '../useUndoStack';
import AddMenu from './AddMenu';
import SceneActionsPanel from './SceneActionsPanel';
import SceneActorsPanel from './SceneActorsPanel';
import SceneOverview from './SceneOverview';
import ScenePhasesPanel from './ScenePhasesPanel';
import SceneSettingsForm from './SceneSettingsForm';
import SceneTimeline from './SceneTimeline';
import { Notice } from './sceneFormParts';
import { readConditions, sceneChanges, writeConditions, type Run } from './sceneChanges';
import { parseScene, typeLabel, type ConditionsTarget, type SceneDto, type Selection } from './sceneModel';
import {
  NO_FILTER, NO_NAMES, questAliasNames, sanitizeFilter, sceneCrumbs, sceneTabItems, tabForSelection,
  type ActionFilter, type SceneTab,
} from './sceneTabModel';
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
  const [tab, setTab] = useState<SceneTab>('overview');
  const [filter, setFilter] = useState<ActionFilter>(NO_FILTER);
  const [revision, setRevision] = useState(0);
  const [failure, setFailure] = useState<FriendlyMessage | null>(null);
  const [picking, setPicking] = useState(false);
  const [conditions, setConditions] = useState<ConditionsTarget | null>(null);
  const [aliasSource, setAliasSource] = useState<{ quest: string; names: ReadonlyMap<number, string> }>({ quest: '', names: NO_NAMES });
  const readOnly = useReadOnly();
  const whenWritable = (action: () => void) => () => { if (!readOnly) action(); };
  const changes = b && sceneKey ? sceneChanges(b, plugin, sceneKey) : null;
  const parentQuest = scene?.parentQuest ?? '';
  const names = aliasSource.quest === parentQuest ? aliasSource.names : NO_NAMES;

  const load = () => {
    if (!b || !sceneKey) return Promise.resolve();
    return Promise.resolve()
      .then(() => b.GetScene(plugin, sceneKey))
      .then(raw => {
        const next = parseScene(raw);
        setScene(next);
        setFilter(current => sanitizeFilter(next, current));
        setLoadError(null);
      })
      .catch(e => setLoadError(errorText(e)))
      .finally(() => setRevision(current => current + 1));
  };

  useEffect(() => { void load(); }, [b, sceneKey, plugin]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    let live = true;
    if (b && parentQuest) {
      Promise.resolve()
        .then(() => b.GetQuestAliases(plugin, parentQuest))
        .then(raw => { if (live) setAliasSource({ quest: parentQuest, names: questAliasNames(raw) }); })
        .catch(() => undefined);
    }
    return () => { live = false; };
  }, [b, plugin, parentQuest]);

  const replay = (step: () => Promise<void>) => () => {
    setFailure(null);
    step().catch(e => setFailure(friendlyError(errorText(e)))).finally(load);
  };

  const select = (next: Selection) => {
    setSelection(next);
    if (!next) return;
    const nextTab = tabForSelection(next);
    setTab(nextTab);
    document.getElementById(`scene-tab-${nextTab}`)?.focus();
  };

  const run: Run = (change, after) => {
    setFailure(null);
    change.apply()
      .then(() => {
        push({ label: change.label, undo: replay(change.revert), redo: replay(change.apply) });
        if (after !== undefined) select(after);
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

  const selectedIndex = (kind: 'phase' | 'actor' | 'action'): number | null =>
    selection?.kind === kind ? selection.index : null;

  return (
    <section ref={scopeRef} tabIndex={-1} className="scene-workspace" onKeyDown={readOnly ? undefined : onKeyDown}>
      <div className="scene-toolbar">
        <button type="button" className="sidebar-action-btn" onClick={() => setPicking(true)}>
          <Search size={14} /> {sceneKey ? 'Open another scene' : 'Pick a scene'}
        </button>
        <button type="button" className="sidebar-action-btn" onClick={whenWritable(undo)} disabled={!canUndo && !readOnly}
          aria-disabled={readOnly} title="Undo the last change (Ctrl+Z)"><Undo2 size={14} /> Undo</button>
        <button type="button" className="sidebar-action-btn" onClick={whenWritable(redo)} disabled={!canRedo && !readOnly}
          aria-disabled={readOnly} title="Redo the change (Ctrl+Y)">Redo</button>
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
          <nav className="scene-breadcrumb" aria-label="Breadcrumb">
            {sceneCrumbs(scene, selection, tab, names).join(' › ')}
          </nav>
          <div className="scene-tabbar">
            <div className="scene-tabbar-tabs">
              <EditorTabs tabs={sceneTabItems(scene)} active={tab} onChange={id => setTab(id as SceneTab)}
                ariaLabel="Scene sections" idPrefix="scene" />
            </div>
            <AddMenu phaseCount={scene.phases.length}
              onAddPhase={() => run(changes.addPhase(scene.phases.length), { kind: 'phase', index: scene.phases.length })}
              onAddActor={() => run(changes.addActor(scene.actors.length), { kind: 'actor', index: scene.actors.length })}
              onAddAction={type => run(changes.addAction(scene.actions.length, type, {
                name: typeLabel(type),
                actorAlias: scene.actors[0]?.alias ?? -1,
                startPhase: 0,
                endPhase: 0,
              }), { kind: 'action', index: scene.actions.length })} />
          </div>
          <div className="scene-tabpanels">
            <EditorTabPanel id="overview" idPrefix="scene" active={tab === 'overview'}>
              <div className="scene-tab-body">
                <SceneOverview scene={scene} names={names} selection={selection} onSelect={select} />
              </div>
            </EditorTabPanel>
            <EditorTabPanel id="timeline" idPrefix="scene" active={tab === 'timeline'}>
              <div className="scene-tab-body">
                {tab === 'timeline' && <SceneTimeline scene={scene} selection={selection} onSelect={select} />}
              </div>
            </EditorTabPanel>
            <EditorTabPanel id="phases" idPrefix="scene" active={tab === 'phases'}>
              <div className="scene-tab-body">
                <ScenePhasesPanel scene={scene} selected={selectedIndex('phase')} revision={revision} changes={changes} run={run}
                  onSelect={index => select({ kind: 'phase', index })} onOpenConditions={openConditions} />
              </div>
            </EditorTabPanel>
            <EditorTabPanel id="actors" idPrefix="scene" active={tab === 'actors'}>
              <div className="scene-tab-body">
                <SceneActorsPanel scene={scene} selected={selectedIndex('actor')} revision={revision} names={names}
                  changes={changes} run={run} onSelect={index => select({ kind: 'actor', index })} />
              </div>
            </EditorTabPanel>
            <EditorTabPanel id="actions" idPrefix="scene" active={tab === 'actions'}>
              <div className="scene-tab-body">
                <SceneActionsPanel scene={scene} selected={selectedIndex('action')} revision={revision} filter={filter}
                  names={names} changes={changes} run={run} active={tab === 'actions'} onFilter={setFilter}
                  onSelect={index => select({ kind: 'action', index })} />
              </div>
            </EditorTabPanel>
            <EditorTabPanel id="settings" idPrefix="scene" active={tab === 'settings'}>
              <div className="scene-tab-body">
                <ReadOnlyFieldset>
                  <SceneSettingsForm key={`scene-${revision}`} scene={scene} changes={changes} run={run}
                    onOpenConditions={openConditions} />
                </ReadOnlyFieldset>
              </div>
            </EditorTabPanel>
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
