import { Eye, EyeOff, AlertTriangle, X } from 'lucide-react';
import CellEditPanel from './CellEditPanel';
import type { CellEditChanges, CellPlacedReference } from './backend';
import type { CellTextureStats } from './CellViewport';

export default function CellViewSection({
  selected, onDeselect, onHide,
  canUndo, canRedo, onEdit, onDelete, onDuplicate, onUndoEdit, onRedoEdit,
  textureStats, onShowTextureDetails,
  hiddenRefCount, canUndoVisibility, canRedoVisibility, onUndoVisibility, onRedoVisibility, onUnhideAll,
  typeGroups, hiddenTypes, onToggleType, onShowAllTypes
}: {
  selected: CellPlacedReference | null;
  onDeselect: () => void;
  onHide: () => void;
  canUndo: boolean;
  canRedo: boolean;
  onEdit: (changes: CellEditChanges) => Promise<boolean>;
  onDelete: () => void;
  onDuplicate: () => void;
  onUndoEdit: () => void;
  onRedoEdit: () => void;
  textureStats: CellTextureStats | null;
  onShowTextureDetails: () => void;
  hiddenRefCount: number;
  canUndoVisibility: boolean;
  canRedoVisibility: boolean;
  onUndoVisibility: () => void;
  onRedoVisibility: () => void;
  onUnhideAll: () => void;
  typeGroups: [string, number][];
  hiddenTypes: Set<string>;
  onToggleType: (type: string) => void;
  onShowAllTypes: () => void;
}) {
  const missingTextures = textureStats ? textureStats.resolveFail + textureStats.decodeFail : 0;
  const showHistoryBar = hiddenRefCount > 0 || canUndoVisibility || canRedoVisibility;

  return (
    <>
      {selected && (
        <div className="cell-selected">
          <div className="cell-selected-head">
            <span>Selected</span>
            <button className="cell-selected-clear" onClick={onDeselect} title="Deselect">
              <X size={12} />
            </button>
          </div>
          <div className="cell-selected-row"><span>Ref</span><strong>{selected.editorId || selected.formKey}</strong></div>
          <div className="cell-selected-row"><span>Type</span><strong>{selected.baseType || selected.recordType}</strong></div>
          <div className="cell-selected-row"><span>Base</span><strong>{selected.baseEditorId || selected.baseFormKey}</strong></div>
          <div className="cell-selected-row">
            <span>Pos</span>
            <strong>{selected.position.x.toFixed(0)}, {selected.position.y.toFixed(0)}, {selected.position.z.toFixed(0)}</strong>
          </div>
          <button className="cell-hide-btn" onClick={onHide}>
            <EyeOff size={12} /> Hide this mesh <kbd>1</kbd>
          </button>
        </div>
      )}

      {selected && (
        <CellEditPanel
          reference={selected}
          canUndo={canUndo}
          canRedo={canRedo}
          onEdit={onEdit}
          onDelete={onDelete}
          onDuplicate={onDuplicate}
          onUndoEdit={onUndoEdit}
          onRedoEdit={onRedoEdit}
        />
      )}

      {textureStats && missingTextures > 0 && (
        <div className="cell-hidden-bar" title={textureStats.firstFailure ?? undefined}>
          <span>
            <AlertTriangle size={12} /> {missingTextures} texture{missingTextures === 1 ? '' : 's'} missing
            {textureStats.ok > 0 ? ` (${textureStats.ok} loaded)` : ''}
          </span>
          <div className="cell-hidden-actions">
            <button className="papyrus-copy" onClick={onShowTextureDetails}>Details</button>
          </div>
        </div>
      )}

      {showHistoryBar && (
        <div className="cell-hidden-bar">
          <span>{hiddenRefCount > 0 ? `${hiddenRefCount} mesh${hiddenRefCount === 1 ? '' : 'es'} hidden` : ''}</span>
          <div className="cell-hidden-actions">
            <button className="papyrus-copy" onClick={onUndoVisibility} disabled={!canUndoVisibility} title="Undo last hide/show">
              Undo
            </button>
            <button className="papyrus-copy" onClick={onRedoVisibility} disabled={!canRedoVisibility} title="Redo last hide/show">
              Redo
            </button>
            {hiddenRefCount > 0 && (
              <button className="papyrus-copy" onClick={onUnhideAll}>Unhide all <kbd>Alt+1</kbd></button>
            )}
          </div>
        </div>
      )}

      {typeGroups.length > 0 && (
        <div className="cell-layers">
          <div className="cell-layers-head">
            <span>Layers ({typeGroups.length})</span>
            {hiddenTypes.size > 0 && <button className="papyrus-copy" onClick={onShowAllTypes}>Show all</button>}
          </div>
          <div className="cell-layers-list">
            {typeGroups.map(([type, count]) => {
              const hidden = hiddenTypes.has(type);
              return (
                <div
                  key={type}
                  className={`cell-layer-row${hidden ? ' hidden' : ''}`}
                  onClick={() => onToggleType(type)}
                  title={hidden ? 'Show' : 'Hide'}
                >
                  {hidden ? <EyeOff size={13} /> : <Eye size={13} />}
                  <span className="cell-layer-name">{type}</span>
                  <span className="cell-layer-count">{count}</span>
                </div>
              );
            })}
          </div>
        </div>
      )}

      <div className="cell-legend">
        <div><span className="cell-swatch cell-swatch-mesh" /> resolved mesh</div>
        <div><span className="cell-swatch cell-swatch-nomodel" /> no model (actor/trap)</div>
        <div><span className="cell-swatch cell-swatch-failed" /> mesh unavailable</div>
      </div>
      <div className="cell-hotkeys">
        <div><kbd>Click</kbd> select · <kbd>1</kbd> hide · <kbd>Alt+1</kbd> unhide all</div>
        <div><kbd>Shift+F</kbd> focus · <kbd>T</kbd> top-down · <kbd>M</kbd> markers · <kbd>D</kbd> deselect</div>
        <div><kbd>Ctrl+Z</kbd> undo edit · <kbd>Ctrl+Y</kbd> redo edit · <kbd>Del</kbd> delete · <kbd>Ctrl+D</kbd> duplicate</div>
        <div><kbd>G</kbd> move gizmo · <kbd>R</kbd> rotate gizmo (drag a selected reference)</div>
      </div>
      <div className="nif-view-hint">Drag to orbit · scroll to zoom · right-drag to pan</div>
    </>
  );
}
