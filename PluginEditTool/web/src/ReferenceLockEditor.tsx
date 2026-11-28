import { useState } from 'react';
import RecordPicker from './RecordPicker';
import type { ReferenceDetails } from './referenceDetails';
import type { ReferenceEditProps } from './ReferencePanel';
import {
  LOCK_LEVELS,
  lockChange,
  lockDraftOf,
  lockLevelLabel,
  parseByte,
  type LockDraft,
} from './util/referenceFlagsLock';

const EMPTY_LOCK: LockDraft = { level: 0, key: null, flags: 0 };

function isEmptyLock(lock: LockDraft): boolean {
  return lock.level === 0 && lock.key === null && lock.flags === 0;
}

export default function ReferenceLockEditor({ lock, edit }: { lock: ReferenceDetails['lock']; edit: ReferenceEditProps }) {
  const base = lock ? lockDraftOf(lock) : EMPTY_LOCK;
  const current = lock && !isEmptyLock(base) ? base : null;
  const [level, setLevel] = useState(base.level);
  const [keyForm, setKeyForm] = useState(base.key);
  const [flagsText, setFlagsText] = useState(String(base.flags));
  const [picking, setPicking] = useState(false);
  const [busy, setBusy] = useState(false);

  const flags = parseByte(flagsText);
  const draft: LockDraft | null = flags === null ? null : { level, key: keyForm, flags };
  const next = draft !== null && !isEmptyLock(draft) ? draft : null;
  const change = draft === null ? undefined : lockChange(current, next);
  const canApply = change !== undefined && !busy;
  const levelOptions = LOCK_LEVELS.some((entry) => entry.level === level)
    ? LOCK_LEVELS
    : [...LOCK_LEVELS, { level, name: lockLevelLabel(level) }];

  const reset = () => {
    setLevel(base.level);
    setKeyForm(base.key);
    setFlagsText(String(base.flags));
  };

  const apply = async () => {
    if (change === undefined || busy) return;
    setBusy(true);
    try {
      if (!(await edit.onEdit({ lock: change }))) reset();
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="ref-edit">
      <label className="ref-edit-field">
        <span>Level</span>
        <select value={level} disabled={busy} onChange={(e) => setLevel(Number(e.currentTarget.value))}>
          {levelOptions.map((entry) => (
            <option key={entry.level} value={entry.level}>{entry.name}</option>
          ))}
        </select>
      </label>
      <div className="ref-edit-field">
        <span>Key</span>
        <span className="ref-lock-key">{keyForm ?? 'None'}</span>
      </div>
      <label className="ref-edit-field">
        <span>Raw flags</span>
        <input
          type="text"
          value={flagsText}
          disabled={busy}
          spellCheck={false}
          onChange={(e) => setFlagsText(e.currentTarget.value)}
        />
      </label>
      {flags === null && <div className="ref-error">Lock flags must be a whole number from 0 to 255.</div>}
      <div className="ref-actions">
        <button className="papyrus-copy" disabled={busy} onClick={() => setPicking(true)}>Pick key</button>
        <button className="papyrus-copy" disabled={busy || keyForm === null} onClick={() => setKeyForm(null)}>Clear key</button>
        <button className="papyrus-copy" disabled={!canApply} onClick={() => void apply()}>Apply lock</button>
      </div>
      {picking && (
        <RecordPicker
          title="Lock key"
          refType="KEYM"
          refTypes="KEYM"
          onPick={(formKey) => { setKeyForm(formKey); setPicking(false); }}
          onClose={() => setPicking(false)}
        />
      )}
    </div>
  );
}
