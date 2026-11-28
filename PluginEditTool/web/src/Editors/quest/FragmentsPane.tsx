import { useRef, useState } from 'react';
import { getBackend } from '../../backend';
import { errorText } from '../../errors';
import { friendlyError, type FriendlyMessage } from '../friendlyErrors';
import HelpTip from '../help/HelpTip';
import type { UndoEntry } from '../useUndoStack';
import { ErrorNote } from './questControls';

type ActionKind = 'display' | 'complete' | 'stage';

interface FragmentAction { kind: ActionKind; value: number; }

interface FragmentArgs {
  stage: number;
  entry: number;
  scriptName: string;
  outputDir: string;
  importsDir: string;
  actions: FragmentAction[];
}

interface Props {
  plugin: string;
  formKey: string;
  push: (entry: UndoEntry) => void;
  onWritten: (importsDir: string) => void;
}

const ACTION_LABELS: Record<ActionKind, string> = {
  display: 'Show objective number',
  complete: 'Complete objective number',
  stage: 'Set quest to stage',
};

const EMPTY_FORM: FragmentArgs = { stage: 0, entry: 0, scriptName: '', outputDir: '', importsDir: '', actions: [{ kind: 'stage', value: 0 }] };

const keyOf = (args: FragmentArgs) => `${args.stage}:${args.entry}`;

export default function FragmentsPane({ plugin, formKey, push, onWritten }: Props) {
  const backend = getBackend();
  const [form, setForm] = useState<FragmentArgs>(EMPTY_FORM);
  const [result, setResult] = useState('');
  const [error, setError] = useState<FriendlyMessage | null>(null);
  const written = useRef(new Map<string, FragmentArgs>());

  const apply = (args: FragmentArgs, onDone?: () => void) => {
    if (!backend) return;
    setError(null);
    backend.SetQuestFragment(plugin, formKey, args.stage, args.entry, args.scriptName,
      args.outputDir, args.importsDir, JSON.stringify(args.actions)).then(raw => {
      written.current.set(keyOf(args), args);
      setResult(typeof raw === 'string'
        ? raw
        : `Wrote function ${raw.function} in script ${raw.script}. Save the plugin to keep the change.`);
      onWritten(args.importsDir);
      onDone?.();
    }, (e: unknown) => setError(friendlyError(errorText(e))));
  };

  const submit = () => {
    const previous = written.current.get(keyOf(form));
    apply(form, () => {
      if (previous) push({
        label: `Script function for stage ${form.stage} log entry ${form.entry}`,
        undo: () => apply(previous),
        redo: () => apply(form),
      });
    });
  };

  const setAction = (at: number, action: FragmentAction) =>
    setForm({ ...form, actions: form.actions.map((current, i) => (i === at ? action : current)) });

  const ready = form.scriptName.trim() !== '' && form.outputDir.trim() !== '' && form.importsDir.trim() !== ''
    && form.actions.length > 0;

  return (
    <section className="quest-pane" aria-labelledby="quest-fragments-title">
      <header className="quest-pane-header">
        <h4 id="quest-fragments-title" tabIndex={-1}>Fragments</h4>
        <HelpTip topicId="quest-fragment" />
      </header>
      <p className="quest-note">
        Pick the stage and log entry that should run a script function. The editor writes a .psc source file and a
        .pex file into the output folder. The source is not read back from the plugin, so this form starts empty.
        Undo works after a second write to the same stage and log entry.
      </p>
      <div className="quest-card">
        <div className="quest-row">
          <label>Stage <input type="number" min={0} max={65535} aria-label="Fragment stage" value={form.stage}
            onChange={e => setForm({ ...form, stage: Number(e.target.value) || 0 })} /></label>
          <label>Log entry <input type="number" min={0} aria-label="Fragment log entry" value={form.entry}
            onChange={e => setForm({ ...form, entry: Number(e.target.value) || 0 })} /></label>
          <HelpTip topicId="quest-stage" />
        </div>
        <div className="quest-row">
          <label>Script name <input type="text" aria-label="Script name" value={form.scriptName}
            onChange={e => setForm({ ...form, scriptName: e.target.value })} /></label>
          <HelpTip topicId="quest-fragment" />
        </div>
        <div className="quest-row">
          <label>Script output folder <input type="text" aria-label="Script output folder" value={form.outputDir}
            onChange={e => setForm({ ...form, outputDir: e.target.value })} /></label>
          <HelpTip topicId="quest-fragment-output" />
        </div>
        <div className="quest-row">
          <label>Imports folder <input type="text" aria-label="Script imports folder" value={form.importsDir}
            onChange={e => setForm({ ...form, importsDir: e.target.value })} /></label>
          <HelpTip topicId="quest-fragment-output" />
        </div>
        {form.actions.map((action, at) => (
          <div key={at} className="quest-row">
            <select aria-label={`Action ${at + 1} kind`} value={action.kind}
              onChange={e => setAction(at, { ...action, kind: e.target.value as ActionKind })}>
              {(Object.keys(ACTION_LABELS) as ActionKind[]).map(kind => <option key={kind} value={kind}>{ACTION_LABELS[kind]}</option>)}
            </select>
            <input type="number" min={0} max={65535} aria-label={`Action ${at + 1} number`} value={action.value}
              onChange={e => setAction(at, { ...action, value: Number(e.target.value) || 0 })} />
            <HelpTip topicId="quest-fragment-action" />
            <button type="button" className="sidebar-action-btn"
              onClick={() => setForm({ ...form, actions: form.actions.filter((_, i) => i !== at) })}>
              Remove action {at + 1}
            </button>
          </div>
        ))}
        <button type="button" className="sidebar-action-btn"
          onClick={() => setForm({ ...form, actions: [...form.actions, { kind: 'stage', value: 0 }] })}>Add action</button>
        <button type="button" className="sidebar-action-btn" disabled={!backend || !ready} onClick={submit}>
          Write script function
        </button>
      </div>
      {result && <p className="quest-note" role="status">{result}</p>}
      {error && <ErrorNote error={error} />}
    </section>
  );
}
