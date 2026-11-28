import { useState } from 'react';
import type { CellEditChanges } from './backend';
import type { ReferenceFlags } from './referenceDetails';
import type { ReferenceEditProps } from './ReferencePanel';
import { PatchChooserHint } from './CellPatchPicker';
import {
  flagCheckboxes,
  flagsChangeAllowed,
  formatRawFlags,
  parseRawFlags,
  withFlag,
  type FlagOption,
} from './util/referenceFlagsLock';

export interface ReferenceMarkers {
  openByDefault: boolean;
  ignoredBySandbox: boolean;
}

export default function ReferenceFlagsEditor({ flags, edit, markers }: {
  flags: ReferenceFlags;
  edit: ReferenceEditProps;
  markers?: ReferenceMarkers;
}) {
  const options = flags.options ?? [];
  if (!edit.patchChosen || options.length === 0) {
    return (
      <>
        {!edit.patchChosen && <PatchChooserHint onChoose={edit.onChoosePatch} />}
        <div className="ref-badges">
          {flags.names.map((flag) => (
            <span key={flag.bit} className="ref-badge">{flag.name}</span>
          ))}
        </div>
        {markers && (
          <>
            <Row label="Open by default">{markers.openByDefault ? 'Yes' : 'No'}</Row>
            <Row label="Ignored by sandbox">{markers.ignoredBySandbox ? 'Yes' : 'No'}</Row>
          </>
        )}
      </>
    );
  }
  return <FlagsForm stored={flags.raw} options={options} markers={markers} onEdit={edit.onEdit} />;
}

function Row({ label, children }: { label: string; children: string }) {
  return (
    <div className="cell-selected-row ref-row">
      <span>{label}</span>
      <strong>{children}</strong>
    </div>
  );
}

function FlagsForm({ stored, options, markers, onEdit }: {
  stored: number;
  options: FlagOption[];
  markers?: ReferenceMarkers;
  onEdit: (changes: CellEditChanges) => Promise<boolean>;
}) {
  const [draft, setDraft] = useState(stored);
  const [rawText, setRawText] = useState(formatRawFlags(stored));
  const [openByDefault, setOpenByDefault] = useState(markers?.openByDefault ?? false);
  const [ignoredBySandbox, setIgnoredBySandbox] = useState(markers?.ignoredBySandbox ?? false);
  const [busy, setBusy] = useState(false);

  const textValid = parseRawFlags(rawText) !== null;
  const flagsChanged = draft !== stored;
  const changes: CellEditChanges = {};
  if (flagsChanged) changes.flags = draft;
  if (markers && openByDefault !== markers.openByDefault) changes.openByDefault = openByDefault;
  if (markers && ignoredBySandbox !== markers.ignoredBySandbox) changes.ignoredBySandbox = ignoredBySandbox;
  const changed = Object.keys(changes).length > 0;
  const allowed = !flagsChanged || flagsChangeAllowed(stored, draft, options);
  const canApply = textValid && changed && allowed && !busy;

  const update = (next: number) => {
    setDraft(next);
    setRawText(formatRawFlags(next));
  };

  const reset = () => {
    update(stored);
    setOpenByDefault(markers?.openByDefault ?? false);
    setIgnoredBySandbox(markers?.ignoredBySandbox ?? false);
  };

  const apply = async () => {
    if (!canApply) return;
    setBusy(true);
    try {
      if (!(await onEdit(changes))) reset();
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="ref-edit">
      <div className="ref-flag-list">
        {flagCheckboxes(draft, options).map((item) => (
          <label key={item.bit} className={item.editable ? 'ref-flag-option' : 'ref-flag-option ref-flag-fixed'}>
            <input
              type="checkbox"
              checked={item.checked}
              disabled={!item.editable || busy}
              onChange={(e) => update(withFlag(draft, item.bit, e.currentTarget.checked))}
            />
            <span>{item.name}</span>
          </label>
        ))}
        {markers && (
          <>
            <label className="ref-flag-option">
              <input
                type="checkbox"
                checked={openByDefault}
                disabled={busy}
                onChange={(e) => setOpenByDefault(e.currentTarget.checked)}
              />
              <span>Open by default</span>
            </label>
            <label className="ref-flag-option">
              <input
                type="checkbox"
                checked={ignoredBySandbox}
                disabled={busy}
                onChange={(e) => setIgnoredBySandbox(e.currentTarget.checked)}
              />
              <span>Ignored by sandbox</span>
            </label>
          </>
        )}
      </div>
      <label className="ref-edit-field">
        <span>Raw flags</span>
        <input
          type="text"
          value={rawText}
          disabled={busy}
          spellCheck={false}
          onChange={(e) => {
            setRawText(e.currentTarget.value);
            const parsed = parseRawFlags(e.currentTarget.value);
            if (parsed !== null) setDraft(parsed);
          }}
        />
      </label>
      {!textValid && <div className="ref-error">Raw flags must be hex, up to 8 digits.</div>}
      {textValid && flagsChanged && !allowed && <div className="ref-error">Only the editable flags can change; the other bits are fixed.</div>}
      <div className="ref-actions">
        <button className="papyrus-copy" disabled={!canApply} onClick={() => void apply()}>Apply flags</button>
      </div>
    </div>
  );
}
