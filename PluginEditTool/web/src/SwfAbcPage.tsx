import { useEffect, useState } from 'react';
import { AlertTriangle } from 'lucide-react';
import { getSwf } from './backend';

export type AbcMethodRow = { index: number; name: string; returnType: string; hasBody: boolean };
export type AbcBlockRow = { abc: number; name: string; classes: string[]; scripts: string[]; methods: AbcMethodRow[] };
export type AbcReply = { changed?: boolean; revision: number; undo: number; redo: number; dirty: boolean; error?: string };

type ErrorReply = { error?: string };
type TextState = { abc: number; loaded: string; draft: string };
type MethodSelection = { abc: number; index: number; label: string };

function parse<T>(reply: string): T & ErrorReply {
  return JSON.parse(reply) as T & ErrorReply;
}

function reason(caught: unknown): string {
  return caught instanceof Error ? caught.message : String(caught);
}

export default function SwfAbcPage({ blocks, error, revision, onApplied }: {
  blocks: AbcBlockRow[];
  error: string;
  revision: number;
  onApplied: (reply: AbcReply) => Promise<void>;
}) {
  const [chosen, setChosen] = useState(0);
  const [text, setText] = useState<TextState | null>(null);
  const [refusal, setRefusal] = useState('');
  const [applyRefusal, setApplyRefusal] = useState('');
  const [method, setMethod] = useState<MethodSelection | null>(null);
  const [instructions, setInstructions] = useState('');

  const block = blocks.find(item => item.abc === chosen) ?? blocks[0];
  const abc = block?.abc ?? -1;
  // Constraint: a draft belongs to the block it was loaded from, so a block switch shows no text until its reply arrives.
  const current = text !== null && text.abc === abc ? text : null;
  const dirty = current !== null && current.draft !== current.loaded;

  useEffect(() => {
    const bridge = getSwf();
    if (!bridge || abc < 0) return;
    let live = true;
    const load = async () => {
      try {
        const reply = parse<{ text?: string }>(await bridge.AbcText(abc));
        if (!live) return;
        if (reply.error) { setRefusal(reply.error); setText(null); return; }
        const loaded = reply.text ?? '';
        setRefusal('');
        // Invariant: a reload refreshes the loaded text but never overwrites an unapplied draft of the same block.
        setText(previous => previous !== null && previous.abc === abc && previous.draft !== previous.loaded
          ? { ...previous, loaded }
          : { abc, loaded, draft: loaded });
      } catch (caught) {
        if (live) { setRefusal(reason(caught)); setText(null); }
      }
    };
    void load();
    return () => { live = false; };
  }, [abc, blocks.length, revision]);

  useEffect(() => {
    const bridge = getSwf();
    if (!bridge || !method) return;
    let live = true;
    const load = async () => {
      try {
        const reply = parse<{ text?: string }>(await bridge.Disassemble(method.abc, method.index));
        if (!live) return;
        if (reply.error) { setRefusal(reply.error); setInstructions(''); return; }
        setRefusal('');
        setInstructions(reply.text ?? '');
      } catch (caught) {
        if (live) setRefusal(reason(caught));
      }
    };
    void load();
    return () => { live = false; };
  }, [method, revision]);

  const apply = async () => {
    const bridge = getSwf();
    if (!bridge || !current) return;
    try {
      const reply = parse<AbcReply>(await bridge.ApplyAbcText(current.abc, current.draft));
      if (reply.error) { setApplyRefusal(reply.error); return; }
      setApplyRefusal('');
      // Requires loaded to match the applied draft first, so the reload may replace it with Radicle's own text.
      setText({ ...current, loaded: current.draft });
      await onApplied(reply);
    } catch (caught) {
      setApplyRefusal(reason(caught));
    }
  };

  const edit = (draft: string) => { if (current) { setApplyRefusal(''); setText({ ...current, draft }); } };
  const revert = () => { if (current) { setApplyRefusal(''); setText({ ...current, draft: current.loaded }); } };
  const chooseBlock = (next: number) => { setApplyRefusal(''); setChosen(next); setMethod(null); setInstructions(''); };
  const pick = (item: AbcBlockRow, row: AbcMethodRow) => {
    setMethod({ abc: item.abc, index: row.index, label: row.name || `method ${row.index}` });
    setInstructions('');
  };

  return (
    <section className="swf-abc-page" aria-label="ABC scripts">
      <aside className="swf-abc-list">
        <h4>Scripts (ABC)</h4>
        {error && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {error}</div>}
        {!error && blocks.length === 0 && <p>No ABC scripts in this SWF.</p>}
        <ul>
          {blocks.map(item => (
            <li key={item.abc}>
              <strong>{item.name || `ABC block ${item.abc}`}</strong>
              <ul>
                {item.classes.map((name, index) => <li key={`class-${index}`}>class {name}</li>)}
                {item.scripts.map((name, index) => <li key={`script-${index}`}>script {name}</li>)}
                {item.methods.map(row => (
                  <li key={row.index}>
                    <button
                      type="button"
                      disabled={!row.hasBody}
                      aria-pressed={method?.abc === item.abc && method.index === row.index}
                      onClick={() => pick(item, row)}
                    >
                      {row.name || `method ${row.index}`} : {row.returnType}
                    </button>
                  </li>
                ))}
              </ul>
            </li>
          ))}
        </ul>
      </aside>
      <main className="swf-abc-main">
        {blocks.length > 1 && (
          <label>
            Block{' '}
            <select value={abc} onChange={event => chooseBlock(Number(event.target.value))}>
              {blocks.map(item => <option key={item.abc} value={item.abc}>{item.name || `ABC block ${item.abc}`}</option>)}
            </select>
          </label>
        )}
        <textarea
          aria-label="ABC script"
          className="swf-abc-text"
          spellCheck={false}
          disabled={!current}
          value={current?.draft ?? ''}
          onChange={event => edit(event.target.value)}
        />
        <div className="swf-abc-actions">
          <button type="button" disabled={!dirty} onClick={() => void apply()}>Apply script</button>
          <button type="button" disabled={!dirty} onClick={revert}>Revert</button>
        </div>
        {applyRefusal && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {applyRefusal}</div>}
        {refusal && <div className="swf-error" role="alert"><AlertTriangle size={14} /> {refusal}</div>}
        {method && <h5>{method.label}</h5>}
        <pre aria-label="instructions">{instructions}</pre>
      </main>
    </section>
  );
}
