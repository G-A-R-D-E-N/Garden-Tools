import { useEffect, useState } from 'react';
import type { ConflictEntry, ConflictMatrix } from '../backend';
import { errorText } from '../errors';
import { useDialogs } from '../useDialogs';
import { pruneSelection, selectRow, type ConflictSelection, type SelectMode } from './conflictSelection';

export interface CopyPick { formKey: string; source: string; editorId?: string }

export function useConflictBatch({ activeTab, treeMode, setStatus, setErrorStr }: {
  activeTab: string;
  treeMode: 'files' | 'conflicts';
  setStatus: (message: string) => void;
  setErrorStr: (message: string) => void;
}) {
  const { pickPlugin: askForTarget, confirm: askConfirm } = useDialogs();
  const [conflicts, setConflicts] = useState<ConflictEntry[] | null>(null);
  const [conflictSel, setConflictSel] = useState<ConflictSelection>({});
  const [conflictAnchor, setConflictAnchor] = useState<string | null>(null);
  const [conflictBusy, setConflictBusy] = useState(false);

  const loadConflicts = () => window.chrome?.webview?.hostObjects?.backend?.GetConflicts()
    .then(raw => {
      const list: ConflictEntry[] = JSON.parse(raw);
      setConflicts(list);
      setConflictSel(sel => pruneSelection(sel, list.map(c => c.FormKey)));
    })
    .catch(e => { setErrorStr(errorText(e)); setConflicts([]); });

  const selectConflict = (formKey: string, mode: SelectMode) => {
    const next = selectRow((conflicts ?? []).map(c => c.FormKey), conflictSel, conflictAnchor, formKey, mode);
    setConflictSel(next.sel);
    setConflictAnchor(next.anchor);
  };

  const clearConflictSelection = () => { setConflictSel({}); setConflictAnchor(null); };

  useEffect(() => {
    if (activeTab === 'explorer' && treeMode === 'conflicts' && conflicts === null) loadConflicts();
  }, [activeTab, treeMode, conflicts]);

  const copyMany = async (picked: CopyPick[]): Promise<boolean> => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return false;
    const chosen = await askForTarget({
      title: `Copy ${picked.length} record(s) as override`,
      description: 'Every selected record is copied into this plugin as an override. It still has to be saved and enabled last in the load order.',
      confirmLabel: 'Copy',
      defaultTarget: 'ConflictPatch.esp',
    });
    if (!chosen) return false;
    const patch = chosen.target;
    setConflictBusy(true);
    setErrorStr('');
    try {
      const items = picked.map(c => ({ formKey: c.formKey, source: c.source }));
      let overwrite = false;
      let allowLosing = false;
      let res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      if (res.requiresOverwrite) {
        const existing: { formKey: string; editorId?: string }[] = Array.isArray(res.existing) ? res.existing : [];
        const sample = existing.slice(0, 5).map(x => `${x.editorId || x.formKey} (${x.formKey})`).join('\n');
        const more = existing.length > 5 ? `\n...and ${existing.length - 5} more.` : '';
        const proceed = await askConfirm({
          title: 'Overwrite existing overrides?',
          message: `${existing.length} of the ${res.total} selected records already exist in ${patch}. ` +
            `Nothing has been copied yet.\n\n${sample}${more}`,
          confirmLabel: 'Overwrite and copy',
          danger: true,
        });
        if (!proceed) {
          setStatus(`Batch copy cancelled -- ${patch} was left unchanged.`);
          return false;
        }
        overwrite = true;
        res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      }
      if (res.requiresLoadOrderConfirmation) {
        const losing: { formKey: string; loadsAfter?: string[] }[] =
          Array.isArray(res.losing) ? res.losing : [];
        const sample = losing.slice(0, 5)
          .map(x => `${x.formKey} (loses to ${(x.loadsAfter ?? []).join(', ')})`).join('\n');
        const more = losing.length > 5 ? `\n...and ${losing.length - 5} more.` : '';
        const proceed = await askConfirm({
          title: 'Overrides will not win',
          message: `${patch} loads before other plugins that override ${losing.length} of the ` +
            `${res.total} selected records, so those copies will lose until ${patch} loads after them. ` +
            `Nothing has been copied yet.\n\n${sample}${more}`,
          confirmLabel: 'Copy anyway',
        });
        if (!proceed) {
          setStatus(`Batch copy cancelled -- ${patch} was left unchanged.`);
          return false;
        }
        allowLosing = true;
        res = JSON.parse(await b.CopyAsOverrideMany(JSON.stringify(items), patch, overwrite, allowLosing));
      }
      const failures: { formKey?: string; reason?: string }[] = Array.isArray(res.failures) ? res.failures : [];
      const failureNote = res.failed ? ` ${res.failed} failed -- see the error list.` : '';
      if (res.failed) {
        const named = (formKey?: string) => {
          const editorId = picked.find(c => c.formKey === formKey)?.editorId;
          return editorId ? `${formKey} (${editorId})` : formKey || 'unknown record';
        };
        const lines = failures.map(f => `${named(f.formKey)}: ${f.reason || 'no reason given'}`);
        if (lines.length === 0) lines.push('(no details returned)');
        setErrorStr([`${res.failed} of ${res.total} records could not be copied into ${patch}:`, ...lines].join('\n'));
      }
      setStatus(`Copied ${res.ok}/${res.total} into ${patch}.${failureNote} Save ${patch} from a record's context menu, then enable it last.`);
      return !res.failed;
    } catch (e) {
      setStatus('Batch copy failed: ' + errorText(e));
      return false;
    } finally { setConflictBusy(false); }
  };

  const batchCopyAsOverride = async () => {
    if (!conflicts) return;
    const picked = conflicts.filter(c => conflictSel[c.FormKey]);
    if (picked.length === 0) { setStatus('Select at least one conflict first.'); return; }
    if (await copyMany(picked.map(c => ({ formKey: c.FormKey, source: c.Winner, editorId: c.EditorID })))) setConflictSel({});
  };

  return {
    conflicts, setConflicts, conflictSel, conflictBusy, loadConflicts, batchCopyAsOverride,
    selectConflict, clearConflictSelection, copyMany,
  };
}

export function useOpenSelectedInTabs() {
  const [comparing, setComparing] = useState(false);
  const compare = async (picked: { formKey: string; plugin: string }[], open: (formKey: string, plugin: string) => Promise<void>) => {
    setComparing(true);
    try {
      for (const p of picked) await open(p.formKey, p.plugin);
    } finally { setComparing(false); }
  };
  return { comparing, compare };
}

export interface CompareColumn { formKey: string; plugin: string }
export interface CompareCell { value: string; differs: boolean }
export interface CompareRow { field: string; label: string; cells: CompareCell[]; differs: boolean }
export interface CompareView { columns: CompareColumn[]; rows: CompareRow[] }

function buildCompareView(picked: { formKey: string; plugin: string; matrix: ConflictMatrix }[]): CompareView {
  const shown = picked.filter(p => p.matrix.Plugins.includes(p.plugin));
  const labels = new Map<string, string>();
  const valuesByColumn = shown.map(p => {
    const col = p.matrix.Plugins.indexOf(p.plugin);
    const values = new Map<string, string>();
    for (const row of p.matrix.Rows) {
      if (!labels.has(row.Field)) labels.set(row.Field, row.DisplayLabel || row.Field);
      values.set(row.Field, row.Values[col] ?? '');
    }
    return values;
  });
  const rows = [...labels.keys()].map(field => {
    const values = valuesByColumn.map(v => v.get(field) ?? '');
    const differs = values.some(v => v !== values[0]);
    return { field, label: labels.get(field) ?? field, cells: values.map(value => ({ value, differs })), differs };
  });
  return { columns: shown.map(p => ({ formKey: p.formKey, plugin: p.plugin })), rows };
}

export function useCompareSelected() {
  const [comparing, setComparing] = useState(false);
  const [view, setView] = useState<CompareView | null>(null);
  const compare = async (picked: { formKey: string; plugin: string }[]) => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    setComparing(true);
    try {
      const loaded: { formKey: string; plugin: string; matrix: ConflictMatrix }[] = await Promise.all(
        picked.map(async p => ({ ...p, matrix: JSON.parse(await b.GetConflictMatrix(p.formKey)) })),
      );
      setView(buildCompareView(loaded));
    } finally { setComparing(false); }
  };
  return { comparing, view, compare, closeView: () => setView(null) };
}
