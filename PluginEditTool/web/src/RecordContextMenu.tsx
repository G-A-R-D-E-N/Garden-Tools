import {
  ArrowDown, ArrowUp, Brain, ClipboardCopy, Copy, Eraser, FilePlus2, Hash, Link2, ListFilter, Minimize2, Minus, Pencil, Plus, Save, Trash2, Eraser as EraserIcon, GitCompare as GitCompareIcon,
} from 'lucide-react';
import type { ConflictFieldRow, ConflictMatrix, ElementActions } from './backend';
import type { useRecordActions } from './recordActions';

const conditionsPathOf = (field: string) => (/(^|\.)Conditions(\[|$)/.test(field) ? field : null);
const initiallyDisabledTypes = new Set(['REFR', 'ACHR', 'NAVM', 'PARW', 'PBAR', 'PBEA', 'PCON', 'PFLA', 'PGRE', 'PHZD', 'PMIS']);

export default function RecordContextMenu({
  menu, matrix, elementActions, actions, onEdit, onEditConditions, onShowTab,
}: {
  menu: { x: number; y: number; row: ConflictFieldRow; col: number };
  matrix: ConflictMatrix;
  elementActions: ElementActions | null;
  actions: ReturnType<typeof useRecordActions>;
  onEdit: (row: ConflictFieldRow, col: number) => void;
  onEditConditions: (plugin: string, path: string) => void;
  onShowTab: (tab: 'conflicts' | 'references') => void;
}) {
  const {
    copyAsOverrideInto, savePlugin, deleteRecord, renumberFormId, compactEsl, cleanMasters, cleanPlugin, copyAsNewRecord,
    deepCopyAsOverride, changeReferencingRecords, removeItm, addMasters, renumberPlugin, createSeq,
    checkCircularLeveled, checkForErrors, findUnreachable, lootReport, updateOnam, compareTo, copyAsDisabledOverrideInto, copyText, elementAction, askImpact,
  } = actions;
  return (
    <div className="rv-context" style={{ left: menu.x, top: menu.y }} onClick={e => e.stopPropagation()}>
      {!menu.row.HasChildren && (
        <button onClick={() => onEdit(menu.row, menu.col)}><Pencil size={13} /> Edit value <span className="rv-key">F2</span></button>
      )}

      { }
      {conditionsPathOf(menu.row.Field) && (
        <button onClick={() => onEditConditions(matrix.Plugins[menu.col], conditionsPathOf(menu.row.Field)!)}><ListFilter size={13} /> Edit conditions…</button>
      )}
      { }
      {elementActions?.canAdd && elementActions.templates.length === 1 && (
        <button onClick={() => elementAction('add', menu.row, menu.col, elementActions.templates[0])}>
          <Plus size={13} /> Add "{elementActions.templates[0]}"
        </button>
      )}
      {elementActions?.canAdd && elementActions.templates.length > 1 && (
        <>
          <div className="rv-context-label">Add</div>
          {elementActions.templates.map(t => (
            <button key={t} onClick={() => elementAction('add', menu.row, menu.col, t)}>
              <Plus size={13} /> {t}
            </button>
          ))}
        </>
      )}
      {elementActions?.canRemove && (
        <button className="rv-danger" onClick={() => elementAction('remove', menu.row, menu.col)}>
          <Minus size={13} /> Remove
        </button>
      )}
      {(elementActions?.canMoveUp || elementActions?.canMoveDown) && (
        <>
          <button disabled={!elementActions?.canMoveUp}
            onClick={() => elementAction('up', menu.row, menu.col)}><ArrowUp size={13} /> Move up</button>
          <button disabled={!elementActions?.canMoveDown}
            onClick={() => elementAction('down', menu.row, menu.col)}><ArrowDown size={13} /> Move down</button>
        </>
      )}
      {elementActions?.canClear && (
        <button className="rv-danger" onClick={() => elementAction('clear', menu.row, menu.col)}>
          <EraserIcon size={13} /> Clear ({elementActions.count})
        </button>
      )}

      <div className="rv-context-sep" />
      { }
      <button onClick={() => copyAsOverrideInto(menu.col)}><Copy size={13} /> Copy as override into...</button>
      {initiallyDisabledTypes.has(matrix.Type) && (
        <button onClick={() => copyAsDisabledOverrideInto(menu.col)}><Copy size={13} /> Copy as disabled override into...</button>
      )}
      <button onClick={() => copyAsNewRecord(menu.col)}><FilePlus2 size={13} /> Copy as new record into...</button>
      <button onClick={() => deepCopyAsOverride(menu.col)}><FilePlus2 size={13} /> Deep copy as override into...</button>
      <div className="rv-context-sep" />
      <button onClick={() => onShowTab('conflicts')}>
        <GitCompareIcon size={13} /> Compare...
      </button>
      <button onClick={() => onShowTab('references')}>
        <Link2 size={13} /> Referenced By
      </button>
      <button onClick={() => changeReferencingRecords()}><Link2 size={13} /> Change referencing records...</button>
      <div className="rv-context-sep" />
      <div className="rv-context-label">Copy to clipboard</div>
      <button onClick={() => copyText('path', menu.row.Field)}><ClipboardCopy size={13} /> Path</button>
      <button onClick={() => copyText('value', menu.row.Values[menu.col] ?? '')}><ClipboardCopy size={13} /> Value</button>
      <button onClick={() => copyText('FormKey', matrix.FormKey)}><ClipboardCopy size={13} /> FormKey</button>
      <button onClick={() => copyText('signature', matrix.Type)}><ClipboardCopy size={13} /> Signature</button>
      <div className="rv-context-sep" />
      <button onClick={() => savePlugin(menu.col)}><Save size={13} /> Save "{matrix.Plugins[menu.col]}"</button>
      <button className="rv-danger" onClick={() => deleteRecord(menu.col)}
        title={`Remove this record from ${matrix.Plugins[menu.col]}`}><Trash2 size={13} /> Remove</button>
      <div className="rv-context-sep" />
      <div className="rv-context-label">Plugin: {matrix.Plugins[menu.col]}</div>
      <button onClick={() => renumberFormId(menu.col)}><Hash size={13} /> Change FormID...</button>
      <button onClick={() => compactEsl(menu.col)}><Minimize2 size={13} /> Compact to ESL</button>
      <button onClick={() => cleanPlugin(menu.col)}><Eraser size={13} /> Clean (UDR)</button>
      <button onClick={() => removeItm(menu.col)}><Eraser size={13} /> Remove identical to master...</button>
      <button onClick={() => addMasters(menu.col)}><Plus size={13} /> Add masters...</button>
      <button onClick={() => cleanMasters(menu.col)}><Eraser size={13} /> Clean masters</button>
      <button onClick={() => renumberPlugin(menu.col)}><Hash size={13} /> Renumber all FormIDs...</button>
      <button onClick={() => createSeq(menu.col)}><FilePlus2 size={13} /> Create SEQ file</button>
      <button onClick={() => checkCircularLeveled(menu.col)}><ListFilter size={13} /> Check circular leveled lists</button>
      <button onClick={() => checkForErrors(menu.col)}><ListFilter size={13} /> Check for errors</button>
      <button onClick={() => findUnreachable(menu.col)}><ListFilter size={13} /> Build reachable info</button>
      <button onClick={() => lootReport(menu.col)}><ListFilter size={13} /> LOOT cleaning report</button>
      <button onClick={() => updateOnam(menu.col)}><ListFilter size={13} /> Update ONAM</button>
      <button onClick={() => compareTo(menu.col)}><ListFilter size={13} /> Compare to...</button>
      <div className="rv-context-sep" />
      <button onClick={askImpact}><Brain size={13} /> Ask AI: what breaks?</button>
    </div>
  );
}
