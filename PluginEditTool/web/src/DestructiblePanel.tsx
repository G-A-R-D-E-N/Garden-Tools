import { useState } from 'react';
import { AlertTriangle, Plus, Save as SaveIcon, Trash2, X } from 'lucide-react';
import './DestructiblePanel.css';

type MemberRow = { signature: string; size: number; hex: string };
type StageValues = Record<string, string>;
type StageRow = { members: MemberRow[]; values?: StageValues };
type ResistanceRow = { damageType: string; value: number };
type GroupView = {
  status: string;
  error: string | null;
  header: { health: number; stageCount: number; flags: number } | null;
  resistances: ResistanceRow[] | null;
  stages: StageRow[] | null;
};
type Reply = {
  path?: string; plugin?: string; dirty?: boolean; saved?: string; reloaded?: boolean;
  formId?: string | null; error?: string | null; group?: GroupView | null;
};

type DestructibleHost = {
  OpenGroupEditor(path: string): Promise<string>;
  ReadGroup(formId: string): Promise<string>;
  InsertGroupHeader(formId: string): Promise<string>;
  RemoveGroupHeader(formId: string): Promise<string>;
  InsertGroupStage(formId: string, index: number): Promise<string>;
  RemoveGroupStage(formId: string, index: number): Promise<string>;
  InsertGroupStageMember(formId: string, stageIndex: number, member: string): Promise<string>;
  RemoveGroupStageMember(formId: string, stageIndex: number, member: string): Promise<string>;
  SetGroupHeader(formId: string, health: number, flags: number): Promise<string>;
  SetGroupStageValue(formId: string, stageIndex: number, field: string, value: string): Promise<string>;
  SetGroupResistanceValue(formId: string, index: number, value: string): Promise<string>;
  SetGroupResistanceDamageType(formId: string, index: number, damageType: string): Promise<string>;
  CreateGroupRecord(signature: string, editorId: string): Promise<string>;
  SaveGroup(): Promise<string>;
};

const OPTIONAL_MEMBERS = ['DSTA', 'DMDL', 'DMDT', 'DMDC', 'DMDS'];

const STAGE_FIELDS = [
  { key: 'healthPercent', label: 'Health %' },
  { key: 'index', label: 'Index' },
  { key: 'modelDamageStage', label: 'Model stage' },
  { key: 'flags', label: 'Flags' },
  { key: 'selfDamage', label: 'Self dmg/s' },
  { key: 'debrisCount', label: 'Debris' },
] as const;

const EMPTY_VALUES: StageValues = Object.fromEntries(STAGE_FIELDS.map(field => [field.key, '']));

function host(): DestructibleHost | null {
  const bridge = (globalThis as { chrome?: { webview?: { hostObjects?: Record<string, DestructibleHost> } } }).chrome;
  return bridge?.webview?.hostObjects?.destructible ?? null;
}

function parse(reply: string): Reply {
  try { return JSON.parse(reply) as Reply; } catch { return { error: reply }; }
}

// Constraint: a refused group renders the status and reason with no stage rows at all. The panel
// must not turn a null stage list into an empty one, because an empty group and an unreadable
// Constraint: group are different states for the author.
export default function DestructiblePanel({ onClose }: { onClose: () => void }) {
  const [path, setPath] = useState('');
  const [formId, setFormId] = useState('000800');
  const [group, setGroup] = useState<GroupView | null>(null);
  const [dirty, setDirty] = useState(false);
  const [opened, setOpened] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [member, setMember] = useState(OPTIONAL_MEMBERS[0]);
  const [health, setHealth] = useState('');
  const [flags, setFlags] = useState('');
  const [stageValues, setStageValues] = useState<Record<number, StageValues>>({});
  const [resistanceValues, setResistanceValues] = useState<string[]>([]);
  const [resistanceLinks, setResistanceLinks] = useState<string[]>([]);
  const [signature, setSignature] = useState('ACTI');
  const [editorId, setEditorId] = useState('');

  const present = group?.status === 'Present';
  const stages = present ? group?.stages ?? [] : null;

  // Constraint: a reply must replace only the state it carries; an absent group, dirty flag or plugin keeps the current one, so a save or a refused edit never blanks the group or disables Save.
  const apply = (reply: Reply) => {
    if (reply.group !== undefined) {
      const next = reply.group;
      const nextPresent = next?.status === 'Present';
      const header = nextPresent && next.header ? next.header : null;
      setHealth(header ? String(header.health) : '');
      setFlags(header ? String(header.flags) : '');
      const values: Record<number, StageValues> = {};
      (nextPresent ? next.stages ?? [] : []).forEach((stage, index) => {
        values[index] = { ...EMPTY_VALUES, ...(stage.values ?? {}) };
      });
      setStageValues(values);
      setResistanceValues((nextPresent ? next.resistances ?? [] : []).map(entry => String(entry.value)));
      setResistanceLinks((nextPresent ? next.resistances ?? [] : []).map(entry => entry.damageType));
      setGroup(next);
    }
    if (reply.dirty !== undefined) setDirty(reply.dirty);
    if (reply.plugin !== undefined) setOpened(reply.plugin);
    setError(reply.error ?? '');
  };

  const send = async (action: (bridge: DestructibleHost) => Promise<string>): Promise<Reply> => {
    const bridge = host();
    if (!bridge) {
      const refusal = { error: 'The editor host is not available.' };
      apply(refusal);
      return refusal;
    }
    setBusy(true);
    try {
      const reply = parse(await action(bridge));
      apply(reply);
      return reply;
    } catch (err) {
      const refusal = { error: err instanceof Error ? err.message : String(err) };
      apply(refusal);
      return refusal;
    } finally { setBusy(false); }
  };

  const open = () => send(bridge => bridge.OpenGroupEditor(path));
  const load = () => send(bridge => bridge.ReadGroup(formId));
  const insertHeader = () => send(bridge => bridge.InsertGroupHeader(formId));
  const removeHeader = () => send(bridge => bridge.RemoveGroupHeader(formId));
  const insertStage = () => send(bridge => bridge.InsertGroupStage(formId, group?.stages?.length ?? 0));
  const removeStage = (index: number) => send(bridge => bridge.RemoveGroupStage(formId, index));
  const insertMember = (index: number) => send(bridge => bridge.InsertGroupStageMember(formId, index, member));
  const removeMember = (index: number, signatureName: string) =>
    send(bridge => bridge.RemoveGroupStageMember(formId, index, signatureName));
  const save = () => send(bridge => bridge.SaveGroup());

  const applyHeader = () => send(bridge => bridge.SetGroupHeader(formId, Number(health), Number(flags)));

  // Constraint: only the fields the author changed are written, so opening a stage and pressing apply never rewrites
  // an unrelated DSTD value the record already carried, so an apply must send only the changed fields.
  const applyStage = async (index: number) => {
    const edited = stageValues[index] ?? EMPTY_VALUES;
    const original: StageValues = stages?.[index]?.values ?? {};
    for (const field of STAGE_FIELDS) {
      const value = (edited[field.key] ?? '').trim();
      if (value === '' || value === (original[field.key] ?? '')) continue;
      if ((await send(bridge => bridge.SetGroupStageValue(formId, index, field.key, value))).error) return;
    }
  };

  // Constraint: only the parts of the entry the author changed are written, so editing a value never retargets the
  // link and editing the link never rewrites the value; both must be addressed by the same DAMC stride index.
  const applyResistance = async (index: number) => {
    const original = group?.resistances?.[index];
    const link = (resistanceLinks[index] ?? '').trim();
    if (original && link !== '' && link !== original.damageType) {
      if ((await send(bridge => bridge.SetGroupResistanceDamageType(formId, index, link))).error) return;
    }
    const value = (resistanceValues[index] ?? '').trim();
    if (value === '' || (original && value === String(original.value))) return;
    await send(bridge => bridge.SetGroupResistanceValue(formId, index, value));
  };

  const create = async () => {
    const bridge = host();
    if (!bridge) { setError('The editor host is not available.'); return; }
    setBusy(true);
    try {
      const reply = parse(await bridge.CreateGroupRecord(signature.trim(), editorId.trim()));
      apply(reply);
      if (reply.formId) setFormId(reply.formId);
    } catch (err) { setError(err instanceof Error ? err.message : String(err)); }
    finally { setBusy(false); }
  };

  const setStageField = (index: number, key: string, value: string) =>
    setStageValues(prev => ({ ...prev, [index]: { ...(prev[index] ?? EMPTY_VALUES), [key]: value } }));

  return (
    <div className="dest-panel">
      <div className="dest-head">
        <span className="dest-title">Destructible group</span>
        {opened && <span className="dest-plugin">{opened}{dirty ? ' · unsaved' : ''}</span>}
        <button className="dest-close" onClick={onClose} title="Close"><X size={16} /></button>
      </div>

      <div className="dest-open">
        <input className="dest-input" placeholder="Data/Fallout4.esm or a plugin path" value={path}
          onChange={event => setPath(event.target.value)} />
        <button className="dest-btn" disabled={busy || !path.trim()} onClick={open}>Open</button>
      </div>

      <div className="dest-open">
        <input className="dest-input" placeholder="000800" value={formId}
          onChange={event => setFormId(event.target.value)} />
        <button className="dest-btn" disabled={busy || !opened} onClick={load}>Load</button>
        <button className="dest-btn" disabled={busy || !opened} onClick={insertHeader}>Insert header</button>
        <button className="dest-btn" disabled={busy || !opened} onClick={removeHeader}>Remove header</button>
        <button className="dest-btn" disabled={busy || !dirty} onClick={save} title="Write the plugin and reload it">
          <SaveIcon size={14} /> Save
        </button>
      </div>

      <div className="dest-open">
        <input className="dest-input dest-input-sm" aria-label="record signature" placeholder="ACTI" value={signature}
          onChange={event => setSignature(event.target.value)} />
        <input className="dest-input" aria-label="editor ID" placeholder="AA_EditorID" value={editorId}
          onChange={event => setEditorId(event.target.value)} />
        <button className="dest-btn" disabled={busy || !opened || !signature.trim() || !editorId.trim()} onClick={create}>
          <Plus size={14} /> Create record
        </button>
      </div>

      {error && <div className="dest-error"><AlertTriangle size={14} /> {error}</div>}

      {group && !present && (
        <div className="dest-refusal">
          <div className="dest-refusal-status">{group.status}</div>
          <div>{group.error ?? 'The group could not be read.'}</div>
        </div>
      )}

      {present && group?.header && (
        <>
          <div className="dest-header">
            health {group.header.health} · stages {group.header.stageCount} · flags {group.header.flags}
          </div>
          <div className="dest-open">
            <label className="dest-value">health
              <input className="dest-input dest-input-sm" value={health}
                onChange={event => setHealth(event.target.value)} />
            </label>
            <label className="dest-value">flags
              <input className="dest-input dest-input-sm" value={flags}
                onChange={event => setFlags(event.target.value)} />
            </label>
            <button className="dest-btn" disabled={busy || !health.trim() || !flags.trim()} onClick={applyHeader}>Apply header</button>
          </div>
        </>
      )}

      {present && (group?.resistances?.length ?? 0) > 0 && (
        <div className="dest-resistances">
          {group!.resistances!.map((_, index) => (
            <label key={index} className="dest-resistance">
              <input className="dest-input dest-input-sm" aria-label={`resistance ${index + 1} damage type`}
                placeholder="damage type"
                value={resistanceLinks[index] ?? ''}
                onChange={event => setResistanceLinks(prev => prev.map((link, at) => at === index ? event.target.value : link))} />
              <input className="dest-input dest-input-sm" aria-label={`resistance ${index + 1}`}
                value={resistanceValues[index] ?? ''}
                onChange={event => setResistanceValues(prev => prev.map((value, at) => at === index ? event.target.value : value))} />
              <button className="dest-btn" disabled={busy} onClick={() => { void applyResistance(index); }}>
                Apply resistance
              </button>
            </label>
          ))}
        </div>
      )}

      {present && (
        <>
          <div className="dest-stages-head">
            <span>Stages</span>
            <button className="dest-btn" disabled={busy} onClick={insertStage}><Plus size={14} /> Add stage</button>
            <select className="dest-select" value={member} onChange={event => setMember(event.target.value)}>
              {OPTIONAL_MEMBERS.map(signatureName => <option key={signatureName} value={signatureName}>{signatureName}</option>)}
            </select>
          </div>
          {(stages ?? []).map((stage, index) => (
            <div key={index} className="dest-stage">
              <div className="dest-stage-head">
                <span>Stage {index + 1} · {stage.members.length} member{stage.members.length === 1 ? '' : 's'}</span>
                <button className="dest-btn" disabled={busy} onClick={() => insertMember(index)}>Add {member}</button>
                <button className="dest-btn" disabled={busy} onClick={() => removeStage(index)} title="Remove stage">
                  <Trash2 size={14} /> Remove
                </button>
              </div>
              <div className="dest-values">
                {STAGE_FIELDS.map(field => (
                  <label key={field.key} className="dest-value">{field.label}
                    <input className="dest-input dest-input-sm" aria-label={`stage ${index + 1} ${field.label}`}
                      value={stageValues[index]?.[field.key] ?? ''}
                      onChange={event => setStageField(index, field.key, event.target.value)} />
                  </label>
                ))}
                <button className="dest-btn" disabled={busy} onClick={() => { void applyStage(index); }}>
                  Apply values
                </button>
              </div>
              {stage.members.map(row => (
                <div key={row.signature} className="dest-member">
                  <span className="dest-sig">{row.signature}</span>
                  <span className="dest-size">{row.size} bytes</span>
                  <span className="dest-hex">{row.hex.slice(0, 48)}</span>
                  <button className="dest-btn" disabled={busy}
                    onClick={() => removeMember(index, row.signature)} title={`Remove ${row.signature}`}>
                    <Trash2 size={12} />
                  </button>
                </div>
              ))}
            </div>
          ))}
        </>
      )}
    </div>
  );
}
