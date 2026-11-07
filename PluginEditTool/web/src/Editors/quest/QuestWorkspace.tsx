import { useEffect, useRef, useState } from 'react';
import { getBackend } from '../../backend';
import { EditorTabPanel, EditorTabs, type EditorTab } from '../EditorTabs';
import HelpTip from '../help/HelpTip';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import { useReadOnly } from '../readOnlyContext';
import { useUndoStack } from '../useUndoStack';
import AliasesPane from './AliasesPane';
import CheckPanel from './CheckPanel';
import FlowChart from './FlowChart';
import FragmentsPane from './FragmentsPane';
import ObjectivesPane from './ObjectivesPane';
import { ErrorNote } from './questControls';
import { PANE_HEADINGS, type CheckFinding, type CheckPane } from './questChecks';
import type { StageSelection } from './questModel';
import StagesPane from './StagesPane';
import { useQuestCheck } from './useQuestCheck';
import { useQuestRecord } from './useQuestRecord';
import './Quest.css';

type QuestTab = 'overview' | CheckPane;

const ID_PREFIX = 'quest';

const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`;

export default function QuestWorkspace({ formKey, plugin }: { formKey: string; plugin: string }) {
  const backend = getBackend();
  const { onKeyDown, scopeRef, push, undo, redo, canUndo, canRedo } = useUndoStack();
  const { snapshot, error, commitStages, editStages, commitObjectives, commitAliases, refreshFragments } = useQuestRecord(plugin, formKey, push);
  const check = useQuestCheck(plugin, formKey, snapshot);
  const readOnly = useReadOnly();
  const whenWritable = (action: () => void) => () => { if (!readOnly) action(); };
  const [selectedStage, setSelectedStage] = useState<StageSelection | null>(null);
  const [tab, setTab] = useState<QuestTab>('overview');
  const pendingFocus = useRef<CheckPane | null>(null);
  const selectStage = (index: number | null) => setSelectedStage(index === null ? null : { index });
  const showStageInStages = (index: number) => {
    setSelectedStage({ index });
    pendingFocus.current = 'stages';
    setTab('stages');
  };
  const openFinding = (finding: CheckFinding) => {
    if (finding.pane === 'stages' && finding.stage !== undefined) showStageInStages(finding.stage);
    else if (finding.pane) {
      pendingFocus.current = finding.pane;
      setTab(finding.pane);
    }
  };
  const onTabChange = (id: string) => {
    pendingFocus.current = null;
    setTab(id as QuestTab);
  };
  const runCheck = () => {
    pendingFocus.current = null;
    setTab('overview');
    check.run();
  };
  useEffect(() => {
    const pane = pendingFocus.current;
    if (pane === null || pane !== tab) return;
    pendingFocus.current = null;
    document.getElementById(PANE_HEADINGS[pane])?.focus();
  }, [tab]);
  const checkReason = !backend ? 'No backend bridge.' : !snapshot ? (error ? 'The quest did not load.' : 'Wait for the quest to load.') : '';
  const findings = check.result?.findings ?? [];
  const tabs: EditorTab[] = [
    { id: 'overview', label: 'Overview', badge: findings.length > 0 ? 'problem' : undefined },
    { id: 'stages', label: 'Stages', count: snapshot?.stages.length },
    { id: 'objectives', label: 'Objectives', count: snapshot?.objectives.length },
    { id: 'aliases', label: 'Aliases', count: snapshot?.aliases.length },
    { id: 'fragments', label: 'Fragments' },
  ];
  return (
    <section ref={scopeRef} tabIndex={-1} className="quest-workspace" onKeyDown={readOnly ? undefined : onKeyDown}>
      <div className="quest-toolbar">
        <span className="quest-current">Editing {formKey}</span>
        <button type="button" className="sidebar-action-btn" disabled={!canUndo && !readOnly} aria-disabled={readOnly}
          onClick={whenWritable(undo)}>Undo</button>
        <button type="button" className="sidebar-action-btn" disabled={!canRedo && !readOnly} aria-disabled={readOnly}
          onClick={whenWritable(redo)}>Redo</button>
        <button type="button" className="sidebar-action-btn" disabled={checkReason !== '' || check.running} onClick={runCheck}
          aria-describedby={checkReason ? 'quest-check-reason' : undefined}>Check this quest</button>
        <HelpTip topicId="quest-check" />
        {checkReason && <span id="quest-check-reason" className="quest-check-reason">{checkReason}</span>}
      </div>
      {error && <ErrorNote error={error} />}
      {snapshot === null && !error && <p className="quest-note">Loading the quest...</p>}
      <EditorTabs tabs={tabs} active={tab} onChange={onTabChange} ariaLabel="Quest sections" idPrefix={ID_PREFIX} />
      <div className="quest-panes">
        <EditorTabPanel id="overview" idPrefix={ID_PREFIX} active={tab === 'overview'}>
          {snapshot && (
            <div className="quest-overview">
              <FlowChart stages={snapshot.stages} objectives={snapshot.objectives} jumps={snapshot.jumps}
                selected={selectedStage} onSelect={selectStage} onEdit={editStages} onEditInStages={showStageInStages} />
              <p className="quest-summary" aria-label="Quest summary">
                <span>{plural(snapshot.stages.length, 'stage')}</span>
                <span>{plural(snapshot.objectives.length, 'objective')}</span>
                <span>{plural(snapshot.aliases.length, 'alias')}</span>
                <span>{check.result ? `${plural(findings.length, 'problem')} found` : 'Not checked yet'}</span>
              </p>
              <CheckPanel result={check.result} running={check.running} error={check.error} outOfDate={check.outOfDate}
                onOpen={openFinding} onRetry={check.run} />
            </div>
          )}
        </EditorTabPanel>
        <EditorTabPanel id="stages" idPrefix={ID_PREFIX} active={tab === 'stages'}>
          {snapshot && (
            <ReadOnlyFieldset>
              <StagesPane stages={snapshot.stages} selected={selectedStage} active={tab === 'stages'} onCommit={commitStages} />
            </ReadOnlyFieldset>
          )}
        </EditorTabPanel>
        <EditorTabPanel id="objectives" idPrefix={ID_PREFIX} active={tab === 'objectives'}>
          {snapshot && (
            <ReadOnlyFieldset>
              <ObjectivesPane objectives={snapshot.objectives} aliases={snapshot.aliases} onCommit={commitObjectives} />
            </ReadOnlyFieldset>
          )}
        </EditorTabPanel>
        <EditorTabPanel id="aliases" idPrefix={ID_PREFIX} active={tab === 'aliases'}>
          {snapshot && (
            <ReadOnlyFieldset>
              <AliasesPane aliases={snapshot.aliases} onCommit={commitAliases} />
            </ReadOnlyFieldset>
          )}
        </EditorTabPanel>
        <EditorTabPanel id="fragments" idPrefix={ID_PREFIX} active={tab === 'fragments'}>
          <ReadOnlyFieldset>
            <FragmentsPane plugin={plugin} formKey={formKey} push={push} onWritten={refreshFragments} />
          </ReadOnlyFieldset>
        </EditorTabPanel>
      </div>
    </section>
  );
}
