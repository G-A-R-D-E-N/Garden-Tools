import { useState } from 'react';
import { getBackend } from '../backend';
import { friendlyError, readOnlySentence } from './friendlyErrors';

interface Props {
  plugin: string;
  formKey: string;
  editablePlugins: readonly string[];
  onCopied?: (target: string) => void;
}

export default function ReadOnlyBanner({ plugin, formKey, editablePlugins, onCopied }: Props) {
  const [picked, setPicked] = useState('');
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const target = editablePlugins.includes(picked) ? picked : (editablePlugins[0] ?? '');

  const copy = async () => {
    const backend = getBackend();
    if (!backend || target === '' || formKey === '') return;
    setBusy(true);
    try {
      const result = await backend.CopyAsOverride(plugin, formKey, target, false);
      setMessage(result);
      onCopied?.(target);
    } catch (error) {
      setMessage(friendlyError(error instanceof Error ? error.message : String(error)).text);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="read-only-banner" role="status">
      <p>{readOnlySentence(plugin)}</p>
      {formKey !== '' && editablePlugins.length > 0 && (
        <div className="read-only-banner-actions">
          <label>
            Copy into
            <select aria-label="Copy into plugin" value={target} onChange={e => setPicked(e.target.value)}>
              {editablePlugins.map(name => <option key={name} value={name}>{name}</option>)}
            </select>
          </label>
          <button type="button" className="sidebar-action-btn" disabled={busy || target === ''} onClick={copy}>
            Copy as override
          </button>
        </div>
      )}
      {message && <p className="read-only-banner-message">{message}</p>}
    </div>
  );
}
