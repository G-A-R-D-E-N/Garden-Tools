import { X } from 'lucide-react';

export default function HelpModal({ onClose }: { onClose: () => void }) {
  return (
    <div className="papyrus-overlay" onClick={onClose}>
      <div className="papyrus-modal glass-panel" style={{ maxWidth: 620 }} onClick={e => e.stopPropagation()}>
        <div className="papyrus-header">
          <span className="papyrus-title">Help</span>
          <button className="papyrus-close" onClick={onClose} title="Close"><X size={16} /></button>
        </div>
        <div style={{ padding: '14px 18px', fontSize: 13, lineHeight: 1.65 }}>
          <p><strong>Loading a modlist.</strong> Use <em>Open MO2</em> and pick the instance folder
            (the one holding <code>mods/</code> and <code>profiles/</code>). On Linux <em>Load Env</em>
            cannot auto-detect a load order, because there is no game-managed <code>Plugins.txt</code>
            outside a Proton prefix; it will say so rather than fail silently.</p>
          <p><strong>Edits are in memory until saved.</strong> Changing a field marks the plugin dirty;
            use Save on that plugin (or the Spreadsheet's Save Changes) to write it to disk.</p>
          <p><strong>Favourites.</strong> Star a record in the right-hand rail and it appears under
            FAVOURITES at the top of the Explorer.</p>
          <p><strong>Search</strong> is <code>Ctrl</code>+<code>K</code>, or the box at the top.</p>
          <p><strong>When something looks wrong,</strong> the log is at
            <code>~/.config/FO4RecordEditor/debug.log</code>. A failed scan now says it failed --
            an empty References tab means nothing references the record, not that the scan broke.</p>
        </div>
      </div>
    </div>
  );
}
