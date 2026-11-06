import { useState } from 'react';
import { ChevronLeft } from 'lucide-react';
import ConditionsEditor from '../ConditionsEditor';
import RecordPicker from '../RecordPicker';
import { getBackend } from '../backend';
import { errorText } from '../errors';
import HelpDrawer from './help/HelpDrawer';
import HelpTip from './help/HelpTip';
import { SCREEN_TOPICS } from './help/helpContent';
import { friendlyError, type FriendlyMessage } from './friendlyErrors';
import { useUndoStack } from './useUndoStack';
import { buildActions } from './dialogue/dialogueActions';
import DialogueChat from './dialogue/DialogueChat';
import DialogueDetails from './dialogue/DialogueDetails';
import DialogueTree from './dialogue/DialogueTree';
import { parseDialogue, pluginOf, type DialogueData, type DialogueInfo, type LineRef } from './dialogue/dialogueModel';
import './dialogue/DialogueScreen.css';

const CONDITIONS_PATH = 'Conditions';

export default function DialogueEditor({ onBack }: { onBack: () => void }) {
  const backend = getBackend();
  const [helpOpen, setHelpOpen] = useState(false);
  const [quest, setQuest] = useState<string | null>(null);
  const [pickingQuest, setPickingQuest] = useState(false);
  const [dialogue, setDialogue] = useState<DialogueData | null>(null);
  const [loading, setLoading] = useState(false);
  const [labels, setLabels] = useState<Record<string, string>>({});
  const [error, setError] = useState<FriendlyMessage | null>(null);
  const [topicKey, setTopicKey] = useState<string | null>(null);
  const [line, setLine] = useState<LineRef | null>(null);
  const [conditionsFor, setConditionsFor] = useState<{ info: DialogueInfo; before: string } | null>(null);
  const { push, onKeyDown, scopeRef } = useUndoStack();

  const fail = (e: unknown) => setError(friendlyError(errorText(e)));

  const refresh = async (questKey: string) => {
    if (!backend) return;
    try {
      const data = parseDialogue(await backend.GetDialogue(pluginOf(questKey), questKey));
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

  const pickQuest = async (questKey: string) => {
    setPickingQuest(false);
    setError(null);
    setQuest(questKey);
    setTopicKey(null);
    setLine(null);
    setDialogue(null);
    setLoading(true);
    await refresh(questKey);
    setLoading(false);
  };

  const openConditions = async (info: DialogueInfo) => {
    if (!backend || !quest) return;
    try {
      const before = await backend.GetConditionsAt(pluginOf(quest), info.formKey, CONDITIONS_PATH);
      setConditionsFor({ info, before });
    } catch (e) {
      fail(e);
    }
  };

  const conditionsSaved = async () => {
    if (!backend || !quest || !conditionsFor) return;
    const { info, before } = conditionsFor;
    const plugin = pluginOf(quest);
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

  const actions = backend && quest ? buildActions(backend, quest, write, push) : null;
  const topic = dialogue?.topics.find(t => t.formKey === topicKey) ?? null;
  const info = topic && line ? (topic.infos.find(i => i.formKey === line.info) ?? null) : null;

  return (
    <section ref={scopeRef} tabIndex={-1} className="editor-screen" aria-labelledby="dialogue-editor-title" onKeyDown={onKeyDown}>
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
        <span>Quest: {quest ?? 'none picked yet'}</span>
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
        <div className="dlg-columns">
          <DialogueTree dialogue={dialogue} selectedTopic={topicKey} actions={actions}
            onSelectTopic={key => { setTopicKey(key); setLine(null); }} />
          <DialogueChat topic={topic} line={line} labels={labels} actions={actions} onSelectLine={setLine} />
          <DialogueDetails info={info} index={line?.index ?? 0} labels={labels} actions={actions}
            onEditConditions={target => void openConditions(target)} />
        </div>
      )}
      {pickingQuest && (
        <RecordPicker title="Quest" refType="Quest" refTypes="QUST" onPick={key => void pickQuest(key)}
          onClose={() => setPickingQuest(false)} />
      )}
      {conditionsFor && quest && (
        <ConditionsEditor plugin={pluginOf(quest)} record={conditionsFor.info.formKey} path={CONDITIONS_PATH}
          label="Reply conditions" onClose={() => setConditionsFor(null)} onSaved={() => void conditionsSaved()} />
      )}
      {helpOpen && <HelpDrawer topicIds={SCREEN_TOPICS.dialogue} onClose={() => setHelpOpen(false)} />}
    </section>
  );
}
