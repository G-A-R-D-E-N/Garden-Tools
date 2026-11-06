import { useState } from 'react';
import HelpTip from '../help/HelpTip';
import type { DialogueActions } from './dialogueActions';
import { textProblem, topicTitle, type DialogueBranch, type DialogueData, type DialogueTopic } from './dialogueModel';

interface TreeProps {
  dialogue: DialogueData;
  selectedTopic: string | null;
  actions: DialogueActions;
  onSelectTopic: (topicKey: string) => void;
}

export default function DialogueTree({ dialogue, selectedTopic, actions, onSelectTopic }: TreeProps) {
  const [adding, setAdding] = useState<'branch' | 'topic' | null>(null);
  const selected = dialogue.topics.find(t => t.formKey === selectedTopic) ?? null;
  const branchKeys = new Set(dialogue.branches.map(b => b.formKey));
  const loose = dialogue.topics.filter(t => !t.branch || !branchKeys.has(t.branch));
  const topicsOf = (branch: DialogueBranch) => dialogue.topics.filter(t => t.branch === branch.formKey);
  const startName = (branch: DialogueBranch) => {
    const start = dialogue.topics.find(t => t.formKey === branch.topic);
    return start ? topicTitle(start) : 'none yet';
  };
  const toggle = (kind: 'branch' | 'topic') => setAdding(current => (current === kind ? null : kind));

  const topicButton = (topic: DialogueTopic) => (
    <li key={topic.formKey}>
      <button type="button" className="dlg-topic" aria-pressed={topic.formKey === selectedTopic}
        onClick={() => onSelectTopic(topic.formKey)}>
        {topicTitle(topic)} <span className="dlg-muted">{topic.editorId}</span>
      </button>
    </li>
  );

  return (
    <nav className="dlg-pane" aria-label="Branches and topics">
      <div className="dlg-pane-head">
        <h4>Branches and topics</h4>
        <HelpTip topicId="dialogue-branch" />
      </div>
      <div className="dlg-row">
        <button type="button" onClick={() => toggle('branch')}>New branch</button>
        <button type="button" onClick={() => toggle('topic')}>New topic</button>
      </div>
      {adding === 'branch' && <BranchForm actions={actions} onDone={() => setAdding(null)} />}
      {adding === 'topic' && <TopicForm branches={dialogue.branches} actions={actions} onDone={() => setAdding(null)} />}
      {dialogue.branches.length === 0 && dialogue.topics.length === 0 && (
        <p className="dlg-empty">This quest has no dialogue yet. Start with a branch, then add a topic to it.</p>
      )}
      {dialogue.branches.map(branch => (
        <div key={branch.formKey} className="dlg-branch">
          <div className="dlg-pane-head">
            <strong>{branch.editorId || branch.formKey}</strong>
            <HelpTip topicId="dialogue-start-topic" />
          </div>
          <p className="dlg-muted">Start topic: {startName(branch)}</p>
          <button type="button" disabled={!selected}
            onClick={() => { if (selected) void actions.setBranchStart(branch, selected); }}>
            Make the selected topic the start
          </button>
          {!selected && <p className="dlg-muted">Pick a topic to make it the start of this branch.</p>}
          <ul className="dlg-list">{topicsOf(branch).map(topicButton)}</ul>
        </div>
      ))}
      {loose.length > 0 && (
        <div className="dlg-branch">
          <strong>Topics without a branch here</strong>
          <ul className="dlg-list">{loose.map(topicButton)}</ul>
        </div>
      )}
    </nav>
  );
}

function BranchForm({ actions, onDone }: { actions: DialogueActions; onDone: () => void }) {
  const [editorId, setEditorId] = useState('');
  const problem = textProblem(editorId);
  const submit = async () => {
    if (problem || editorId.trim() === '') return;
    if (await actions.createBranch(editorId.trim())) onDone();
  };
  return (
    <form className="dlg-form" onSubmit={e => { e.preventDefault(); void submit(); }}>
      <div className="dlg-field-head">
        <label htmlFor="dlg-branch-id">Editor ID</label>
        <HelpTip topicId="dialogue-editor-id" />
      </div>
      <input id="dlg-branch-id" value={editorId} onChange={e => setEditorId(e.target.value)} />
      {problem && <p role="alert" className="dlg-problem">{problem}</p>}
      <button type="submit" disabled={problem !== null || editorId.trim() === ''}>Create branch</button>
    </form>
  );
}

function TopicForm({ branches, actions, onDone }: { branches: DialogueBranch[]; actions: DialogueActions; onDone: () => void }) {
  const [editorId, setEditorId] = useState('');
  const [name, setName] = useState('');
  const [priority, setPriority] = useState('50');
  const [branch, setBranch] = useState('');
  const priorityValue = Number(priority);
  const priorityOk = priority.trim() !== '' && Number.isFinite(priorityValue);
  const problem = textProblem(name) ?? textProblem(editorId);
  const ready = problem === null && name.trim() !== '' && priorityOk;
  const submit = async () => {
    if (!ready) return;
    const created = await actions.createTopic({ editorId: editorId.trim(), name: name.trim(), priority: priorityValue, branch: branch || null });
    if (created) onDone();
  };
  return (
    <form className="dlg-form" onSubmit={e => { e.preventDefault(); void submit(); }}>
      <div className="dlg-field-head">
        <label htmlFor="dlg-topic-name">Player line</label>
        <HelpTip topicId="dialogue-topic" />
      </div>
      <input id="dlg-topic-name" value={name} onChange={e => setName(e.target.value)} />
      <div className="dlg-field-head">
        <label htmlFor="dlg-topic-id">Editor ID</label>
        <HelpTip topicId="dialogue-editor-id" />
      </div>
      <input id="dlg-topic-id" value={editorId} onChange={e => setEditorId(e.target.value)} />
      <div className="dlg-field-head">
        <label htmlFor="dlg-topic-priority">Priority</label>
        <HelpTip topicId="dialogue-priority" />
      </div>
      <input id="dlg-topic-priority" type="number" value={priority} onChange={e => setPriority(e.target.value)} />
      <div className="dlg-field-head">
        <label htmlFor="dlg-topic-branch">Branch</label>
        <HelpTip topicId="dialogue-branch" />
      </div>
      <select id="dlg-topic-branch" value={branch} onChange={e => setBranch(e.target.value)}>
        <option value="">No branch</option>
        {branches.map(b => <option key={b.formKey} value={b.formKey}>{b.editorId || b.formKey}</option>)}
      </select>
      {problem && <p role="alert" className="dlg-problem">{problem}</p>}
      <button type="submit" disabled={!ready}>Create topic</button>
    </form>
  );
}
