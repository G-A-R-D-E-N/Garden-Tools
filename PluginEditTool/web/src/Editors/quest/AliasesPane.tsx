import { useState } from 'react';
import type { ConditionDto } from '../../backend';
import ConditionsEditor from '../../ConditionsEditor';
import HelpTip from '../help/HelpTip';
import { CommitInput } from './questControls';
import { ALIAS_KINDS, type AliasKind, type QuestAlias } from './questModel';

interface Props {
  plugin: string;
  aliases: QuestAlias[];
  onCommit: (next: QuestAlias[], label: string) => void;
}

export default function AliasesPane({ plugin, aliases, onCommit }: Props) {
  const [problem, setProblem] = useState('');
  const [edit, setEdit] = useState<{ at: number; aliases: QuestAlias[] } | null>(null);
  const editingAt = edit?.aliases === aliases ? edit.at : null;
  const setEditingAt = (at: number | null) => setEdit(at === null ? null : { at, aliases });

  const replace = (position: number, next: QuestAlias, label: string) =>
    onCommit(aliases.map((alias, i) => (i === position ? next : alias)), label);

  const addAlias = () => {
    const id = aliases.reduce((next, alias) => Math.max(next, alias.id + 1), 0);
    onCommit([...aliases, { id, kind: 'reference', name: '', flags: null, collectionEntries: [] }], `Add alias ${id}`);
  };

  const setKind = (position: number, alias: QuestAlias, kind: AliasKind) => {
    if (kind === 'collection' && alias.collectionEntries.length === 0) {
      setProblem('A collection needs at least one entry, and this editor cannot add entries yet. Keep the kind as Reference or Location.');
      return;
    }
    setProblem('');
    replace(position, { ...alias, kind }, `Change alias ${alias.id} kind`);
  };

  const setFlags = (position: number, alias: QuestAlias, text: string) => {
    const flags = text.trim() === '' ? null : Number(text);
    if (flags !== null && (!Number.isInteger(flags) || flags < 0)) {
      setProblem('Alias flags must be a whole number of 0 or more, or empty.');
      return;
    }
    setProblem('');
    replace(position, { ...alias, flags }, `Change alias ${alias.id} flags`);
  };

  return (
    <section className="quest-pane" aria-labelledby="quest-aliases-title">
      <header className="quest-pane-header">
        <h4 id="quest-aliases-title" tabIndex={-1}>Aliases</h4>
        <HelpTip topicId="alias" />
        <button type="button" className="sidebar-action-btn" onClick={addAlias}>Add alias</button>
      </header>
      <div className="quest-row">
        <p className="quest-warning">
          Saving rewrites the whole alias list. Fill rules, keywords and items are kept when you save, but
          this editor cannot show or edit them. Use Conditions on a reference or location alias to edit its conditions.
        </p>
        <HelpTip topicId="quest-alias-fill" />
      </div>
      {problem && <p className="quest-warning" role="alert">{problem}</p>}
      {aliases.length === 0 && (
        <div className="editor-empty">This quest has no aliases yet. Add one with Add alias for an NPC, a door or a place the quest uses.</div>
      )}
      <div className="quest-card-grid">
        {aliases.map((alias, position) => (
          <div key={position} className="quest-card">
            <div className="quest-row">
              <strong>Alias {alias.id}</strong>
              <button type="button" className="sidebar-action-btn"
                onClick={() => onCommit(aliases.filter((_, i) => i !== position), `Remove alias ${alias.id}`)}>
                Remove alias {alias.id}
              </button>
            </div>
            <div className="quest-row">
              <span>Kind</span>
              <HelpTip topicId="quest-alias-kind" />
              <select aria-label={`Alias ${alias.id} kind`} value={alias.kind}
                onChange={e => setKind(position, alias, e.target.value as AliasKind)}>
                {ALIAS_KINDS.map(kind => <option key={kind} value={kind}>{kind}</option>)}
              </select>
            </div>
            <div className="quest-row">
              <span>Name</span>
              <HelpTip topicId="quest-alias-name" />
              <CommitInput type="text" aria-label={`Alias ${alias.id} name`} value={alias.name}
                onCommit={name => replace(position, { ...alias, name }, `Change alias ${alias.id} name`)} />
            </div>
            <div className="quest-row">
              <span>Flags (number)</span>
              <HelpTip topicId="quest-alias-flags" />
              <CommitInput type="number" min={0} aria-label={`Alias ${alias.id} flags`}
                value={alias.flags === null ? '' : String(alias.flags)} placeholder="None"
                onCommit={text => setFlags(position, alias, text)} />
            </div>
            {alias.kind === 'collection' && (
              <p className="quest-note">
                This collection holds {alias.collectionEntries.length} entr{alias.collectionEntries.length === 1 ? 'y' : 'ies'}.
                The editor keeps them as they are.
              </p>
            )}
            {alias.kind !== 'collection' && (alias.conditionsUnreadable ? (
              <p className="quest-note" role="note">Conditions could not be read. Saving keeps them.</p>
            ) : (
              <div className="quest-row">
                <button type="button" className="sidebar-action-btn" onClick={() => setEditingAt(position)}>
                  Conditions ({alias.conditions?.length ?? 0})
                </button>
              </div>
            ))}
            {editingAt === position && alias.kind !== 'collection' && !alias.conditionsUnreadable && (
              <ConditionsEditor plugin={plugin} label={`Alias ${alias.id}`}
                onClose={() => setEditingAt(null)}
                draft={{
                  rows: (alias.conditions ?? []) as ConditionDto[],
                  onApply: rows => {
                    setEditingAt(null);
                    if (JSON.stringify(rows) === JSON.stringify(alias.conditions ?? [])) return;
                    replace(position, { ...alias, conditions: rows }, `Change alias ${alias.id} conditions`);
                  },
                }} />
            )}
          </div>
        ))}
      </div>
    </section>
  );
}
