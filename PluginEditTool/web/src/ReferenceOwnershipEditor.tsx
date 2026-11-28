import { useState } from 'react';
import RecordPicker from './RecordPicker';
import type { ReferenceDetails } from './referenceDetails';
import type { ReferenceEditProps } from './ReferencePanel';
import { ownershipChange, parseFactionRank, type OwnershipState } from './util/referenceOwnershipPrimitive';

type OwnerSignature = 'NPC_' | 'FACT';

export default function ReferenceOwnershipEditor({ ownership, factionRank, edit }: {
  ownership: ReferenceDetails['ownership'];
  factionRank: number | null;
  edit: ReferenceEditProps;
}) {
  const storedOwner = ownership?.owner?.formKey ?? null;
  const storedSignature = ownership?.owner?.signature ?? null;
  const stored: OwnershipState = { owner: storedOwner, noCrime: ownership?.noCrime ?? false, rank: factionRank };
  const storedRankText = factionRank === null ? '' : String(factionRank);

  const [owner, setOwner] = useState<string | null>(storedOwner);
  const [ownerSignature, setOwnerSignature] = useState<string | null>(storedSignature);
  const [noCrime, setNoCrime] = useState(stored.noCrime);
  const [rankText, setRankText] = useState(storedRankText);
  const [picking, setPicking] = useState<OwnerSignature | null>(null);
  const [busy, setBusy] = useState(false);

  const isFaction = owner !== null && ownerSignature === 'FACT';
  const rank = parseFactionRank(rankText);
  const rankInvalid = rank === undefined || (rank !== null && !isFaction);
  const draftRank = isFaction && typeof rank === 'number' ? rank : null;
  const change = ownershipChange(stored, { owner, noCrime, rank: draftRank });
  const canApply = change !== undefined && !rankInvalid && !busy;

  const reset = () => {
    setOwner(storedOwner);
    setOwnerSignature(storedSignature);
    setNoCrime(stored.noCrime);
    setRankText(storedRankText);
  };

  const apply = async () => {
    if (!canApply || change === undefined) return;
    setBusy(true);
    try {
      if (!(await edit.onEdit({ ownership: change }))) reset();
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="ref-edit">
      <div className="ref-edit-field">
        <span>Owner</span>
        <span className="ref-lock-key">{owner ?? 'None'}</span>
      </div>
      <div className="ref-actions">
        <button className="papyrus-copy" disabled={busy} onClick={() => setPicking('NPC_')}>Pick NPC</button>
        <button className="papyrus-copy" disabled={busy} onClick={() => setPicking('FACT')}>Pick faction</button>
        <button
          className="papyrus-copy"
          disabled={busy || owner === null}
          onClick={() => {
            setOwner(null);
            setOwnerSignature(null);
            setRankText('');
          }}
        >
          Clear owner
        </button>
      </div>
      <label className="ref-flag-option">
        <input type="checkbox" checked={noCrime} disabled={busy} onChange={(e) => setNoCrime(e.currentTarget.checked)} />
        <span>No crime</span>
      </label>
      <label className="ref-edit-field">
        <span>Faction rank</span>
        <input
          type="text"
          inputMode="numeric"
          value={rankText}
          disabled={busy || !isFaction}
          spellCheck={false}
          onChange={(e) => setRankText(e.currentTarget.value)}
        />
      </label>
      {rankInvalid && (
        <div className="ref-error">
          {rank === undefined ? 'Faction rank must be a whole number.' : 'Faction rank needs a faction owner.'}
        </div>
      )}
      <div className="ref-actions">
        <button className="papyrus-copy" disabled={!canApply} onClick={() => void apply()}>Apply owner</button>
      </div>
      {picking && (
        <RecordPicker
          title={picking === 'FACT' ? 'Owner faction' : 'Owner NPC'}
          refType={picking}
          refTypes={picking}
          onPick={(formKey) => {
            setOwner(formKey);
            setOwnerSignature(picking);
            if (picking === 'NPC_') setRankText('');
            setPicking(null);
          }}
          onClose={() => setPicking(null)}
        />
      )}
    </div>
  );
}
