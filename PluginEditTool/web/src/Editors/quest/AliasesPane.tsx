import { useState } from 'react';
import HelpTip from '../help/HelpTip';
import { CommitInput } from './questControls';
import { ALIAS_KINDS, type AliasKind, type QuestAlias } from './questModel';

interface Props {
  aliases: QuestAlias[];
  onCommit: (next: QuestAlias[], label: string) => void;
}

export default function AliasesPane({ aliases, onCommit }: Props) {
  const [problem, setProblem] = useState('');

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
          Saving rewrites the whole alias list. This editor cannot show fill rules, keywords or items yet, so saving can
          remove them. Check each alias in the Creation Kit after you save.
        </p>
        <HelpTip topicId="quest-alias-fill" />
      </div>
      {problem && <p className="quest-warning" role="alert">{problem}</p>}
      {aliases.length === 0 && (
        <div className="editor-empty">This quest has no aliases yet. Add one with Add alias for an NPC, a door or a place the quest uses.</div>
      )}
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
        </div>
      ))}
    </section>
  );
}
