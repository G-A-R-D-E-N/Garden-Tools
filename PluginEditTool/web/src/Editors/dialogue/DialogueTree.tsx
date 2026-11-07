import { useMemo, useState } from 'react';
import HelpTip from '../help/HelpTip';
import ReadOnlyFieldset from '../ReadOnlyFieldset';
import type { DialogueActions } from './dialogueActions';
import { textProblem, topicTitle, type DialogueBranch, type DialogueData, type DialogueTopic } from './dialogueModel';

export const TOPIC_PAGE_SIZE = 50;

const LOOSE_KEY = 'loose';

interface TreeProps {
  dialogue: DialogueData;
  active: boolean;
  selectedTopic: string | null;
  actions: DialogueActions;
  onSelectTopic: (topicKey: string) => void;
  onCreateTopic: (topicKey: string) => void;
}

interface Section {
  key: string;
  branch: DialogueBranch | null;
  topics: DialogueTopic[];
}

interface Entry {
  section: Section;
  topic: DialogueTopic | null;
}

interface Run {
  section: Section;
  entries: Entry[];
}

interface Reveal {
  section: string;
  topic: string | null;
}

const matchesQuery = (topic: DialogueTopic, needle: string): boolean =>
  [topic.editorId, topic.formKey, topicTitle(topic), topic.name ?? '']
    .some(text => text.toLowerCase().includes(needle));

function buildSections(dialogue: DialogueData, needle: string): Section[] {
  const keep = (topic: DialogueTopic) => needle === '' || matchesQuery(topic, needle);
  const branchKeys = new Set(dialogue.branches.map(b => b.formKey));
  const sections: Section[] = dialogue.branches
    .map(branch => ({
      key: branch.formKey,
      branch,
      topics: dialogue.topics.filter(t => t.branch === branch.formKey && keep(t)),
    }))
    .filter(section => needle === '' || section.topics.length > 0);
  const loose = dialogue.topics.filter(t => (!t.branch || !branchKeys.has(t.branch)) && keep(t));
  if (loose.length > 0) sections.push({ key: LOOSE_KEY, branch: null, topics: loose });
  return sections;
}

function paginate(sections: Section[], collapsed: ReadonlySet<string>): Entry[][] {
  const pages: Entry[][] = [[]];
  let slots = 0;
  for (const section of sections) {
    const open = !collapsed.has(section.key);
    if (!open || section.topics.length === 0) {
      pages[pages.length - 1].push({ section, topic: null });
      continue;
    }
    for (const topic of section.topics) {
      if (slots === TOPIC_PAGE_SIZE) {
        pages.push([]);
        slots = 0;
      }
      pages[pages.length - 1].push({ section, topic });
      slots += 1;
    }
  }
  return pages;
}

function pageOf(pages: Entry[][], reveal: Reveal): number {
  const byTopic = reveal.topic === null
    ? -1
    : pages.findIndex(page => page.some(entry => entry.topic?.formKey === reveal.topic));
  if (byTopic >= 0) return byTopic;
  return pages.findIndex(page => page.some(entry => entry.section.key === reveal.section));
}

function groupRuns(entries: Entry[]): Run[] {
  const runs: Run[] = [];
  for (const entry of entries) {
    const last = runs.at(-1);
    if (last && last.section.key === entry.section.key) last.entries.push(entry);
    else runs.push({ section: entry.section, entries: [entry] });
  }
  return runs;
}

const replyWord = (count: number): string => `${count} ${count === 1 ? 'reply' : 'replies'}`;

export default function DialogueTree({
  dialogue, active, selectedTopic, actions, onSelectTopic, onCreateTopic,
}: TreeProps) {
  const [adding, setAdding] = useState<'branch' | 'topic' | null>(null);
  const [query, setQuery] = useState('');
  const [page, setPage] = useState(0);
  const [collapsed, setCollapsed] = useState<Set<string>>(() => new Set());
  const [reveal, setReveal] = useState<Reveal | null>(null);
  const selected = dialogue.topics.find(t => t.formKey === selectedTopic) ?? null;
  const needle = query.trim().toLowerCase();
  const sections = useMemo(() => buildSections(dialogue, needle), [dialogue, needle]);
  const pages = useMemo(() => paginate(sections, collapsed), [sections, collapsed]);
  const revealed = reveal === null ? -1 : pageOf(pages, reveal);
  const current = Math.min(revealed >= 0 ? revealed : page, pages.length - 1);
  const runs = groupRuns(pages[current] ?? []);
  const topicCount = sections.reduce((count, section) => count + section.topics.length, 0);
  const toggle = (kind: 'branch' | 'topic') => setAdding(prev => (prev === kind ? null : kind));
  const toggleSection = (key: string) => setCollapsed(prev => {
    const next = new Set(prev);
    if (next.has(key)) next.delete(key);
    else next.add(key);
    return next;
  });
  const revealAfterCreate = (section: string, topic: string | null) => {
    setQuery('');
    setCollapsed(prev => {
      if (!prev.has(section)) return prev;
      const next = new Set(prev);
      next.delete(section);
      return next;
    });
    setReveal({ section, topic });
  };
  const branchCreated = (formKey: string | null) => {
    setAdding(null);
    if (formKey) revealAfterCreate(formKey, null);
  };
  const topicCreated = (formKey: string | null, section: string) => {
    setAdding(null);
    if (formKey) onCreateTopic(formKey);
    revealAfterCreate(section, formKey);
  };
  const startName = (branch: DialogueBranch) => {
    const start = dialogue.topics.find(t => t.formKey === branch.topic);
    return start ? topicTitle(start) : 'none yet';
  };

  const topicRow = (topic: DialogueTopic, branch: DialogueBranch | null) => (
    <li key={topic.formKey}>
      <button type="button" className="dlg-topic" aria-pressed={topic.formKey === selectedTopic}
        onClick={() => onSelectTopic(topic.formKey)}>
        <span className="dlg-topic-title">
          {topicTitle(topic)}
          {branch?.topic === topic.formKey && <span className="dlg-chip">Start topic</span>}
        </span>
        <span className="dlg-muted">{topic.editorId} {'\u00B7'} {replyWord(topic.infos.length)}</span>
      </button>
    </li>
  );

  return (
    <nav className="dlg-pane dlg-topics" aria-label="Branches and topics">
      <div className="dlg-pane-head">
        <h4>Topics</h4>
        <HelpTip topicId="dialogue-branch" />
      </div>
      <div className="dlg-toolbar">
        <input type="search" aria-label="Search topics" placeholder="Search by editor ID, form key or name"
          value={query} onChange={e => { setQuery(e.target.value); setReveal(null); setPage(0); }} />
        <button type="button" onClick={() => toggle('branch')}>New branch</button>
        <button type="button" onClick={() => toggle('topic')}>New topic</button>
      </div>
      {adding === 'branch' && (
        <ReadOnlyFieldset><BranchForm actions={actions} onCreated={branchCreated} /></ReadOnlyFieldset>
      )}
      {adding === 'topic' && (
        <ReadOnlyFieldset>
          <TopicForm branches={dialogue.branches} actions={actions} onCreated={topicCreated} />
        </ReadOnlyFieldset>
      )}
      {dialogue.branches.length === 0 && dialogue.topics.length === 0 && (
        <p className="dlg-empty">This quest has no dialogue yet. Start with a branch, then add a topic to it.</p>
      )}
      {needle !== '' && topicCount === 0 && <p className="dlg-empty">No topic matches "{query.trim()}".</p>}
      {topicCount > 0 && (
        <div className="dlg-pager">
          <span className="dlg-muted">{topicCount} {topicCount === 1 ? 'topic' : 'topics'}</span>
          {pages.length > 1 && (
            <>
              <button type="button" disabled={current === 0}
                onClick={() => { setReveal(null); setPage(current - 1); }}>Previous</button>
              <span className="dlg-muted">Page {current + 1} of {pages.length}</span>
              <button type="button" disabled={current >= pages.length - 1}
                onClick={() => { setReveal(null); setPage(current + 1); }}>Next</button>
            </>
          )}
        </div>
      )}
      {active && runs.map(run => {
        const { section } = run;
        const { branch } = section;
        const open = !collapsed.has(section.key);
        return (
          <div key={section.key} className="dlg-branch">
            <button type="button" className="dlg-group-head" aria-expanded={open}
              onClick={() => toggleSection(section.key)}>
              <span>{branch ? (branch.editorId || branch.formKey) : 'Topics without a branch'}</span>
              <span className="dlg-chip">{section.topics.length}</span>
            </button>
            {open && branch && (
              <div className="dlg-group-body">
                <p className="dlg-muted">Start topic: {startName(branch)}</p>
                <ReadOnlyFieldset>
                  <button type="button" disabled={!selected}
                    onClick={() => { if (selected) void actions.setBranchStart(branch, selected); }}>
                    Make the selected topic the start
                  </button>
                </ReadOnlyFieldset>
                {!selected && <p className="dlg-muted">Pick a topic to make it the start of this branch.</p>}
              </div>
            )}
            {open && section.topics.length === 0 && <p className="dlg-muted">No topics in this branch yet.</p>}
            {open && section.topics.length > 0 && (
              <ul className="dlg-list">
                {run.entries.flatMap(entry => (entry.topic ? [topicRow(entry.topic, branch)] : []))}
              </ul>
            )}
          </div>
        );
      })}
    </nav>
  );
}

function BranchForm({ actions, onCreated }: { actions: DialogueActions; onCreated: (formKey: string | null) => void }) {
  const [editorId, setEditorId] = useState('');
  const problem = textProblem(editorId);
  const submit = async () => {
    if (problem || editorId.trim() === '') return;
    const made = await actions.createBranch(editorId.trim());
    if (made.ok) onCreated(made.formKey);
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

function TopicForm({ branches, actions, onCreated }: {
  branches: DialogueBranch[];
  actions: DialogueActions;
  onCreated: (formKey: string | null, section: string) => void;
}) {
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
    const made = await actions.createTopic({ editorId: editorId.trim(), name: name.trim(), priority: priorityValue, branch: branch || null });
    if (made.ok) onCreated(made.formKey, branch || LOOSE_KEY);
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
