import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import ConditionsEditor from '../ConditionsEditor';
import RecordPicker, { type PickedRecord } from '../RecordPicker';
import { getBackend } from '../backend';
import { errorText } from '../errors';
import HelpDrawer from './help/HelpDrawer';
import HelpTip from './help/HelpTip';
import { SCREEN_TOPICS } from './help/helpContent';
import { friendlyError, type FriendlyMessage } from './friendlyErrors';
import { useUndoStack } from './useUndoStack';
import WritableScope from './WritableScope';
import { useWritableTarget } from './useWritableTarget';
import { EditorTabPanel, EditorTabs, type EditorTab } from './EditorTabs';
import { buildActions } from './dialogue/dialogueActions';
import DialogueChat from './dialogue/DialogueChat';
import DialogueDetails from './dialogue/DialogueDetails';
import DialogueScripts from './dialogue/DialogueScripts';
import DialogueTree from './dialogue/DialogueTree';
import {
  parseDialogue, topicTitle, type DialogueData, type DialogueInfo, type LineRef,
} from './dialogue/dialogueModel';
import './dialogue/DialogueScreen.css';

const CONDITIONS_PATH = 'Conditions';

type DialogueTab = 'conversation' | 'topics' | 'details' | 'scripts';

export default function DialogueEditor({ onBack }: { onBack: () => void }) {
  const backend = getBackend();
  const [helpOpen, setHelpOpen] = useState(false);
  const [quest, setQuest] = useState<PickedRecord | null>(null);
  const [pickingQuest, setPickingQuest] = useState(false);
  const [dialogue, setDialogue] = useState<DialogueData | null>(null);
  const [loading, setLoading] = useState(false);
  const [labels, setLabels] = useState<Record<string, string>>({});
  const [error, setError] = useState<FriendlyMessage | null>(null);
  const [topicKey, setTopicKey] = useState<string | null>(null);
  const [line, setLine] = useState<LineRef | null>(null);
  const [tab, setTab] = useState<DialogueTab>('topics');
  const [conditionsFor, setConditionsFor] = useState<{ info: DialogueInfo; before: string } | null>(null);
  const { push, reset, onKeyDown, scopeRef } = useUndoStack();
  const { readOnly } = useWritableTarget(quest?.plugin ?? '');

  const fail = (e: unknown) => setError(friendlyError(errorText(e)));

  const refresh = async (picked: PickedRecord) => {
    if (!backend) return;
    try {
      const data = parseDialogue(await backend.GetDialogue(picked.plugin, picked.formKey));
      setDialogue(data);
      const emotions = [...new Set(data.topics.flatMap(t => t.infos.flatMap(i =>
        i.responses.flatMap(r => (r.emotion ? [r.emotion] : [])))))];
      if (emotions.length > 0) {
        const got: Record<string, string> = JSON.parse(await backend.ResolveFormKeyLabels(emotions.join(',')));
        setLabels(prev => ({ ...prev, ...got }));
      }
    } catch (e) {
      fail(e);
    }
  };

  const write = async (call: () => Promise<unknown>): Promise<boolean> => {
    setError(null);
    try {
      await call();
    } catch (e) {
      fail(e);
      return false;
    }
    if (quest) await refresh(quest);
    return true;
  };

  const pickQuest = async (picked: PickedRecord) => {
    setPickingQuest(false);
    setError(null);
    reset();
    setQuest(picked);
    setTopicKey(null);
    setLine(null);
    setTab('topics');
    setDialogue(null);
    setLoading(true);
    await refresh(picked);
    setLoading(false);
  };

  const openConditions = async (info: DialogueInfo) => {
    if (!backend || !quest) return;
    try {
      const before = await backend.GetConditionsAt(quest.plugin, info.formKey, CONDITIONS_PATH);
      setConditionsFor({ info, before });
    } catch (e) {
      fail(e);
    }
  };

  const conditionsSaved = async () => {
    if (!backend || !quest || !conditionsFor) return;
    const { info, before } = conditionsFor;
    const plugin = quest.plugin;
    setConditionsFor(null);
    const restore = (raw: string) => () => {
      void write(() => backend.SetConditionsAt(plugin, info.formKey, CONDITIONS_PATH, raw));
    };
    try {
      const after = await backend.GetConditionsAt(plugin, info.formKey, CONDITIONS_PATH);
      if (after !== before) push({ label: 'Change reply conditions', undo: restore(before), redo: restore(after) });
    } catch (e) {
      fail(e);
    }
    await refresh(quest);
  };

  const openTab = (next: DialogueTab) => {
    setTab(next);
    document.getElementById(`dlg-tab-${next}`)?.focus();
  };

  const actions = backend && quest ? buildActions(backend, quest.plugin, quest.formKey, write, push) : null;
  const topic = dialogue?.topics.find(t => t.formKey === topicKey) ?? null;
  const info = topic && line ? (topic.infos.find(i => i.formKey === line.info) ?? null) : null;
  const branch = topic ? dialogue?.branches.find(b => b.formKey === topic.branch) : undefined;
  const replyNumber = topic && info ? topic.infos.indexOf(info) + 1 : 0;
  const crumbs = quest && dialogue ? [
    quest.formKey,
    ...(topic ? [branch ? (branch.editorId || branch.formKey) : 'No branch', topicTitle(topic)] : []),
    ...(replyNumber > 0 ? [`Reply ${replyNumber}`] : []),
  ] : [];
  const tabs: EditorTab[] = [
    { id: 'conversation', label: 'Conversation', count: topic ? topic.infos.length : undefined },
    { id: 'topics', label: 'Topics', count: dialogue?.topics.length },
    { id: 'details', label: 'Details' },
    { id: 'scripts', label: 'Conditions & scripts' },
  ];

  return (
    <section ref={scopeRef} tabIndex={-1} className="editor-screen" aria-labelledby="dialogue-editor-title"
      onKeyDown={readOnly ? undefined : onKeyDown}>
      <div className="editor-screen-body dlg-body">
        <header className="editor-screen-header">
          <button type="button" className="sidebar-action-btn" onClick={onBack}><ChevronLeft size={14} /> Back</button>
          <h3 id="dialogue-editor-title">Dialogue</h3>
          <button type="button" className="sidebar-action-btn" aria-expanded={helpOpen}
            onClick={() => setHelpOpen(open => !open)}>Help</button>
        </header>
        <p className="editor-purpose">
          Dialogue is what characters say. Pick a quest, then a topic, to see the conversation and edit each NPC line.
        </p>
        <div className="dlg-row">
          <span>Quest: {quest?.formKey ?? 'none picked yet'}</span>
          <HelpTip topicId="dialogue-quest" />
          <button type="button" onClick={() => setPickingQuest(true)}>{quest ? 'Change quest' : 'Pick a quest'}</button>
        </div>
        {error && (
          <div role="alert" className="dlg-problem">
            <p>{error.text}</p>
            {error.details && <details><summary>Details</summary><code>{error.details}</code></details>}
          </div>
        )}
        {!backend && <div className="editor-empty">The backend is not connected, so dialogue cannot be read.</div>}
        {backend && !quest && (
          <div className="editor-empty">Pick a quest to see its dialogue. Each quest owns its own branches, topics and replies.</div>
        )}
        {quest && loading && <div className="editor-empty">Reading the quest's dialogue.</div>}
        {quest && dialogue && actions && (
          <WritableScope plugin={quest.plugin} formKey={quest.formKey} showCopy={false}>
            <div className="dlg-workspace">
              <nav className="dlg-crumbs" aria-label="Location">{crumbs.join(' › ')}</nav>
              <EditorTabs tabs={tabs} active={tab} onChange={id => setTab(id as DialogueTab)}
                ariaLabel="Dialogue sections" idPrefix="dlg" />
              <div className="dlg-tab-scroll">
                <EditorTabPanel id="conversation" idPrefix="dlg" active={tab === 'conversation'}>
                  <DialogueChat topic={topic} line={line} labels={labels} actions={actions}
                    onSelectLine={ref => { setLine(ref); openTab('details'); }} />
                </EditorTabPanel>
                <EditorTabPanel id="topics" idPrefix="dlg" active={tab === 'topics'}>
                  <DialogueTree dialogue={dialogue} active={tab === 'topics'} selectedTopic={topicKey} actions={actions}
                    onSelectTopic={key => { setTopicKey(key); setLine(null); openTab('conversation'); }}
                    onCreateTopic={key => { setTopicKey(key); setLine(null); }} />
                </EditorTabPanel>
                <EditorTabPanel id="details" idPrefix="dlg" active={tab === 'details'}>
                  <DialogueDetails info={info} index={line?.index ?? 0} labels={labels} actions={actions}
                    active={tab === 'details'} />
                </EditorTabPanel>
                <EditorTabPanel id="scripts" idPrefix="dlg" active={tab === 'scripts'}>
                  <DialogueScripts info={info} onEditConditions={target => void openConditions(target)} />
                </EditorTabPanel>
              </div>
            </div>
          </WritableScope>
        )}
        {pickingQuest && (
          <RecordPicker title="Quest" refType="Quest" refTypes="QUST"
            onPick={(formKey, plugin) => void pickQuest({ formKey, plugin })} onClose={() => setPickingQuest(false)} />
        )}
        {conditionsFor && quest && (
          <ConditionsEditor plugin={quest.plugin} record={conditionsFor.info.formKey} path={CONDITIONS_PATH}
            label="Reply conditions" onClose={() => setConditionsFor(null)} onSaved={() => void conditionsSaved()} />
        )}
      </div>
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.dialogue} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}
