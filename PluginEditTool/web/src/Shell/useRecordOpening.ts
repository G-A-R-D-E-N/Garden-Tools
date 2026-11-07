import { useCallback, useRef, useState } from 'react';
import type { ConflictMatrix, SearchHit } from '../backend';
import { errorMessage } from '../errors';
import type { RecordNode } from './ExplorerTree';

interface OpenRec { title: string; plugin: string; matrix: ConflictMatrix }

export function useRecordOpening(setErrorStr: (message: string) => void) {
  const [openRecords, setOpenRecords] = useState<OpenRec[]>([]);
  const [activeKey, setActiveKey] = useState<string>('');
  const [recordLoading, setRecordLoading] = useState(false);
  const openSeq = useRef(0);
  const reloadSeq = useRef(0);

  const openRecord = openRecords.find(r => r.matrix?.FormKey === activeKey) ?? null;

  const putRecord = useCallback((rec: OpenRec) => {
    if (!rec.matrix?.FormKey) return;
    setOpenRecords(prev => {
      const at = prev.findIndex(r => r.matrix?.FormKey === rec.matrix.FormKey);
      if (at === -1) return [...prev, rec];
      const next = [...prev];
      next[at] = rec;
      return next;
    });
    setActiveKey(rec.matrix.FormKey);
  }, []);

  const closeRecordTab = useCallback((formKey: string) => {
    setOpenRecords(prev => {
      const at = prev.findIndex(r => r.matrix?.FormKey === formKey);
      if (at === -1) return prev;
      const next = prev.filter(r => r.matrix?.FormKey !== formKey);

      setActiveKey(cur => (cur !== formKey ? cur : (next[at] ?? next[at - 1])?.matrix?.FormKey ?? ''));
      return next;
    });
  }, []);

  const openRecordInView = async (path: string, node: RecordNode) => {
    const app = window.chrome?.webview?.hostObjects?.appInterop;
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!app || !back) { setErrorStr("Record bridge unavailable."); return; }
    const mine = ++openSeq.current;
    setRecordLoading(true);
    try {
      const formKey = await app.OpenRecord(path);
      if (mine !== openSeq.current) return;
      if (!formKey) { setErrorStr(`Couldn't resolve a FormKey for ${node.Key}.`); return; }
      const plugin = path.split(/[\\/]/)[0];
      const raw = await back.GetConflictMatrix(formKey);
      if (mine !== openSeq.current) return;
      const matrix: ConflictMatrix | null = raw ? JSON.parse(raw) : null;
      if (!matrix) {
        setErrorStr(`Couldn't build a view of ${node.Key} (${formKey}). The environment may still be `
          + `loading or was just reloaded -- try again, or reload it with Open MO2.`);
        return;
      }
      putRecord({ title: node.Key, plugin, matrix });
    } catch (err) {
      if (mine === openSeq.current) setErrorStr(errorMessage(err) ?? String(err));
    } finally {
      if (mine === openSeq.current) setRecordLoading(false);
    }
  };

  const openHit = async (hit: SearchHit) => {
    const back = window.chrome?.webview?.hostObjects?.backend;
    if (!back) return;
    const mine = ++openSeq.current;
    setRecordLoading(true);
    try {
      const json = await back.GetConflictMatrix(hit.FormKey);
      if (mine !== openSeq.current) return;
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${hit.FormKey}.`); return; }
      putRecord({ title: hit.EditorID || hit.FormKey, plugin: hit.Plugin, matrix });
    } catch (err) {
      if (mine === openSeq.current) setErrorStr(errorMessage(err) ?? String(err));
    } finally {
      if (mine === openSeq.current) setRecordLoading(false);
    }
  };

  const openByFormKey = async (formKey: string, pluginName: string) => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    const mine = ++openSeq.current;
    setRecordLoading(true);
    try {
      const json = await b.GetConflictMatrix(formKey);
      if (mine !== openSeq.current) return;
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;
      if (!matrix) { setErrorStr(`No record for ${formKey}.`); return; }
      putRecord({ title: matrix.EditorID || formKey, plugin: pluginName || matrix.Winner, matrix });
    } catch (err) {
      if (mine === openSeq.current) setErrorStr(errorMessage(err) ?? String(err));
    } finally {
      if (mine === openSeq.current) setRecordLoading(false);
    }
  };

  const reloadMatrix = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b || !openRecord) return;
    const mine = ++reloadSeq.current;
    try {
      const json = await b.GetConflictMatrix(openRecord.matrix.FormKey);
      if (mine !== reloadSeq.current) return;
      const matrix: ConflictMatrix | null = json ? JSON.parse(json) : null;

      if (!matrix || !matrix.Plugins || matrix.Plugins.length === 0) closeRecordTab(openRecord.matrix.FormKey);
      else setOpenRecords(prev => prev.map(r => (r.matrix.FormKey === matrix.FormKey ? { ...r, matrix } : r)));
    } catch (err) {
      if (mine === reloadSeq.current) setErrorStr(errorMessage(err) ?? String(err));
    }
  };

  return {
    openRecords, activeKey, setActiveKey, recordLoading, openRecord, closeRecordTab,
    openRecordInView, openHit, openByFormKey, reloadMatrix,
  };
}
