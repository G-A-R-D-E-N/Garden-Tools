import { useEffect, useState, type ReactNode } from 'react';
import {
  getBackend, type Backend, type ConflictMatrix, type EntryOp, type FactionRecord, type LeveledList,
  type NpcRecord, type WeaponDamageTypes,
} from './backend';
import { errorText } from './errors';
import './TypedRecordPanel.css';

type Row = Record<string, unknown>;
type Column = { key: string; label: string; numeric?: boolean; editable?: boolean; format?: (value: unknown) => string };
type Props = { matrix: ConflictMatrix; plugin: string; readOnly: boolean; onChanged: () => Promise<void> };

const LEVELED_TYPES = ['LVLI', 'LVLN', 'LVSP'];
const LEVELED_COLUMNS: Column[] = [
  { key: 'level', label: 'Level', numeric: true },
  { key: 'item', label: 'Item' },
  { key: 'count', label: 'Count', numeric: true },
  { key: 'chanceNone', label: 'Chance none', numeric: true },
  { key: 'extra', label: 'Extra', editable: false, format: yesNo },
];
const FACTION_COLUMNS: Column[] = [{ key: 'faction', label: 'Faction' }, { key: 'rank', label: 'Rank', numeric: true }];
const PERK_COLUMNS: Column[] = [{ key: 'perk', label: 'Perk' }, { key: 'rank', label: 'Rank', numeric: true }];
const INVENTORY_COLUMNS: Column[] = [
  { key: 'item', label: 'Item' },
  { key: 'count', label: 'Count', numeric: true },
  { key: 'extra', label: 'Extra', editable: false, format: yesNo },
];
const RELATION_COLUMNS: Column[] = [
  { key: 'faction', label: 'Faction' },
  { key: 'modifier', label: 'Modifier', numeric: true },
  { key: 'reaction', label: 'Reaction' },
];
const RANK_COLUMNS: Column[] = [
  { key: 'number', label: 'Rank', numeric: true },
  { key: 'maleTitle', label: 'Male title', editable: false, format: titleText },
  { key: 'femaleTitle', label: 'Female title', editable: false, format: titleText },
  { key: 'hasInsignia', label: 'Insignia', editable: false, format: yesNo },
];

function yesNo(value: unknown): string { return value ? 'yes' : ''; }
function text(value: unknown): string { return value === null || value === undefined ? '' : String(value); }
function titleText(value: unknown): string {
  if (typeof value === 'string') return value;
  if (typeof value !== 'object' || value === null) return '';
  if ('stringId' in value) return `string ${value.stringId}`;
  if ('rawId' in value) return `raw ${value.rawId}`;
  return '';
}
const num = (input: string) => (input.trim() === '' ? Number.NaN : Number(input));

const readLeveled = (b: Backend, key: string) => b.GetLeveledList(key);
const readNpc = (b: Backend, key: string) => b.GetNpc(key);
const readFaction = (b: Backend, key: string) => b.GetFaction(key);
const readWeapon = (b: Backend, key: string) => b.GetWeaponDamageTypes(key);

function useTypedRecord<T>(matrix: ConflictMatrix, read: (b: Backend, key: string) => Promise<string | object>, onChanged: () => Promise<void>) {
  const formKey = matrix.FormKey;
  const [loaded, setLoaded] = useState<{ key: string; data: T } | null>(null);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let live = true;
    (async () => {
      try {
        const b = getBackend();
        if (!b) throw new Error('No backend bridge.');
        const raw: unknown = await read(b, formKey);
        const parsed = (typeof raw === 'string' ? JSON.parse(raw) : raw) as T;
        if (live) { setLoaded({ key: formKey, data: parsed }); setError(''); }
      } catch (e) {
        if (live) setError('Could not read record: ' + errorText(e));
      }
    })();
    return () => { live = false; };
  }, [formKey, matrix, read]);

  const edit = async (failure: string, run: (b: Backend) => Promise<string>) => {
    setBusy(true);
    try {
      const b = getBackend();
      if (!b) throw new Error('No backend bridge.');
      const message = await run(b);
      setError('');
      setStatus(message);
      await onChanged();
    } catch (e) {
      setStatus('');
      setError(`${failure}: ${errorText(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const data = loaded && loaded.key === formKey ? loaded.data : null;
  return { data, error, status, busy, edit };
}

function Frame({ error, status, children }: { error: string; status: string; children?: ReactNode }) {
  return (
    <div className="rv-list-tab">
      {error && <div className="tp-error" role="alert">{error}</div>}
      {status && <div className="rv-tab-note">{status}</div>}
      {children}
    </div>
  );
}

function Pending({ error, status }: { error: string; status: string }) {
  return <Frame error={error} status={status}>{!error && <div className="rv-tab-loading">Reading the record…</div>}</Frame>;
}

function SettingsForm({ chanceNone, maxCount, flags, readOnly, busy, onSave }: {
  chanceNone: number; maxCount: number; flags: number; readOnly: boolean; busy: boolean;
  onSave: (chanceNone: number, maxCount: number, flags: number) => void;
}) {
  const [draft, setDraft] = useState({ chanceNone: String(chanceNone), maxCount: String(maxCount), flags: String(flags) });
  return (
    <div className="tp-form">
      <label>Chance none<input type="number" disabled={readOnly} value={draft.chanceNone} onChange={e => setDraft({ ...draft, chanceNone: e.target.value })} /></label>
      <label>Max count<input type="number" disabled={readOnly} value={draft.maxCount} onChange={e => setDraft({ ...draft, maxCount: e.target.value })} /></label>
      <label>Flags<input type="number" disabled={readOnly} value={draft.flags} onChange={e => setDraft({ ...draft, flags: e.target.value })} /></label>
      {!readOnly && <button className="button" disabled={busy} onClick={() => onSave(num(draft.chanceNone), num(draft.maxCount), num(draft.flags))}>Save settings</button>}
    </div>
  );
}

function ListEditor({ title, columns, rows, readOnly, busy, appendOnly, onApply }: {
  title: string; columns: Column[]; rows: Row[]; readOnly: boolean; busy: boolean; appendOnly?: boolean;
  onApply: (op: EntryOp, index: number, value?: Row) => void;
}) {
  const editable = columns.filter(c => c.editable !== false);
  const [index, setIndex] = useState(String(rows.length));
  const [form, setForm] = useState<Record<string, string>>({});

  const pick = (i: number) => {
    setIndex(String(i));
    setForm(Object.fromEntries(editable.map(c => [c.key, text(rows[i]?.[c.key])])));
  };

  const noRowPicked = appendOnly === true && !(num(index) < rows.length);

  const apply = (op: EntryOp) => {
    const at = num(index);
    if (op === 'remove') { onApply(op, at); return; }
    const parsed: Row = Object.fromEntries(editable.map(c => [c.key, c.numeric ? num(form[c.key] ?? '') : form[c.key] ?? '']));
    onApply(op, at, op === 'set' ? { ...rows[at], ...parsed } : parsed);
  };

  return (
    <fieldset className="tp-fieldset">
      <legend className="tp-heading">{title}</legend>
      <table className="rv-flat-table">
        <thead><tr><th>#</th>{columns.map(c => <th key={c.key}>{c.label}</th>)}</tr></thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={i} className={String(i) === index ? 'tp-picked' : undefined} tabIndex={0} aria-selected={String(i) === index}
              onClick={() => pick(i)} onKeyDown={e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(i); } }}>
              <td className="rv-num">{i}</td>
              {columns.map(c => <td key={c.key}>{(c.format ?? text)(row[c.key])}</td>)}
            </tr>
          ))}
        </tbody>
      </table>
      {rows.length === 0 && <div className="rv-tab-empty">No entries.</div>}
      {!readOnly && (
        <div className="tp-form">
          {editable.map(c => (
            <label key={c.key}>{c.label}
              <input type={c.numeric ? 'number' : 'text'} value={form[c.key] ?? ''}
                onChange={e => setForm(f => ({ ...f, [c.key]: e.target.value }))} />
            </label>
          ))}
          {!appendOnly && <label>Index<input type="number" value={index} onChange={e => setIndex(e.target.value)} /></label>}
          <button className="button" disabled={busy} onClick={() => apply('add')}>Add</button>
          <button className="button" disabled={busy || noRowPicked} onClick={() => apply('set')}>Set</button>
          <button className="button" disabled={busy || noRowPicked} onClick={() => apply('remove')}>Remove</button>
        </div>
      )}
    </fieldset>
  );
}

function LeveledListPanel({ matrix, plugin, readOnly, onChanged }: Props) {
  const formKey = matrix.FormKey;
  const { data, error, status, busy, edit } = useTypedRecord<LeveledList>(matrix, readLeveled, onChanged);
  if (!data) return <Pending error={error} status={status} />;
  return (
    <Frame error={error} status={status}>
      <h3 className="tp-heading">Settings</h3>
      <SettingsForm key={`${data.chanceNone}/${data.maxCount}/${data.flags}`} chanceNone={data.chanceNone}
        maxCount={data.maxCount} flags={data.flags} readOnly={readOnly} busy={busy}
        onSave={(c, m, f) => edit('Settings edit failed', b => b.SetLeveledListSettings(plugin, formKey, c, m, f))} />
      <ListEditor key={data.entries.length} title="Entries" columns={LEVELED_COLUMNS} rows={data.entries} readOnly={readOnly} busy={busy}
        onApply={(op, index, value) => edit('Entry edit failed', b => b.EditLeveledListEntry(plugin, formKey, op, index, value))} />
    </Frame>
  );
}

function NpcPanel({ matrix, plugin, readOnly, onChanged }: Props) {
  const formKey = matrix.FormKey;
  const { data, error, status, busy, edit } = useTypedRecord<NpcRecord>(matrix, readNpc, onChanged);
  if (!data) return <Pending error={error} status={status} />;
  const apply = (list: 'factions' | 'perks' | 'inventory') =>
    (op: EntryOp, index: number, value?: Row) => edit('NPC edit failed', b => b.EditNpcEntries(plugin, formKey, list, op, index, value));
  return (
    <Frame error={error} status={status}>
      <ListEditor key={`factions-${data.factions.length}`} title="Factions" columns={FACTION_COLUMNS} rows={data.factions} readOnly={readOnly} busy={busy} appendOnly onApply={apply('factions')} />
      <ListEditor key={`perks-${data.perks.length}`} title="Perks" columns={PERK_COLUMNS} rows={data.perks} readOnly={readOnly} busy={busy} appendOnly onApply={apply('perks')} />
      <ListEditor key={`inventory-${data.inventory.length}`} title="Inventory" columns={INVENTORY_COLUMNS} rows={data.inventory} readOnly={readOnly} busy={busy} appendOnly onApply={apply('inventory')} />
    </Frame>
  );
}

function FactionPanel({ matrix, plugin, readOnly, onChanged }: Props) {
  const formKey = matrix.FormKey;
  const { data, error, status, busy, edit } = useTypedRecord<FactionRecord>(matrix, readFaction, onChanged);
  if (!data) return <Pending error={error} status={status} />;
  const apply = (list: 'relations' | 'ranks') =>
    (op: EntryOp, index: number, value?: Row) => edit('Faction edit failed', b => b.EditFactionEntries(plugin, formKey, list, op, index, value));
  return (
    <Frame error={error} status={status}>
      <ListEditor key={`relations-${data.relations.length}`} title="Relations" columns={RELATION_COLUMNS} rows={data.relations} readOnly={readOnly} busy={busy} appendOnly onApply={apply('relations')} />
      <ListEditor key={`ranks-${data.ranks.length}`} title="Ranks" columns={RANK_COLUMNS} rows={data.ranks} readOnly={readOnly} busy={busy} appendOnly onApply={apply('ranks')} />
    </Frame>
  );
}

function WeaponPanel({ matrix, onChanged }: Props) {
  const { data, error, status } = useTypedRecord<WeaponDamageTypes>(matrix, readWeapon, onChanged);
  if (!data) return <Pending error={error} status={status} />;
  return (
    <Frame error={error} status={status}>
      <h3 className="tp-heading">Damage types (read-only)</h3>
      <table className="rv-flat-table">
        <thead><tr><th>Damage type</th><th>Amount</th><th>Curve table</th></tr></thead>
        <tbody>
          {data.damageTypes.map((d, i) => (
            <tr key={i}>
              <td className="rv-mono">{d.type ?? ''}</td>
              <td className="rv-num">{d.amount}</td>
              <td className="rv-mono">{d.curveTable ?? ''}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {data.damageTypes.length === 0 && <div className="rv-tab-empty">No damage types.</div>}
    </Frame>
  );
}

export default function TypedRecordPanel({ matrix, plugin, onChanged }: {
  matrix: ConflictMatrix; plugin: string; onChanged: () => Promise<void>;
}) {
  const readOnly = plugin !== matrix.Winner;
  const props = { matrix, plugin, readOnly, onChanged };
  const panel = LEVELED_TYPES.includes(matrix.Type) ? <LeveledListPanel {...props} />
    : matrix.Type === 'NPC_' ? <NpcPanel {...props} />
    : matrix.Type === 'FACT' ? <FactionPanel {...props} />
    : matrix.Type === 'WEAP' ? <WeaponPanel {...props} />
    : null;
  return (
    <>
      {readOnly && panel && matrix.Type !== 'WEAP' && <div className="rv-tab-note">{`Showing the winning version from ${matrix.Winner}. Open the record from ${matrix.Winner} to edit.`}</div>}
      {panel}
    </>
  );
}
