import { useEffect, useState } from 'react';
import type { AppInterop, LoadOrderSummary, Mo2Status, RecordTypeEntry } from '../backend';
import { errorMessage } from '../errors';
import { useDialogs } from '../useDialogs';
import type { RecordNode } from './ExplorerTree';

const parseMo2Status = (raw: unknown): Mo2Status | null => {
  try {
    const value = typeof raw === 'string' ? JSON.parse(raw) : raw;
    if (!value || typeof value !== 'object') return null;
    return value as Mo2Status;
  } catch {
    return null;
  }
};

export function useEnvironment({ setErrorStr, setProgress }: {
  setErrorStr: (message: string) => void;
  setProgress: (value: number | null) => void;
}) {
  const { confirm: askConfirm } = useDialogs();
  const [plugins, setPlugins] = useState<RecordNode[]>([]);
  const [backend, setBackend] = useState<AppInterop | null>(null);
  const [envLoading, setEnvLoading] = useState(false);
  const [mo2Status, setMo2Status] = useState<Mo2Status | null>(null);
  const [recordTypes, setRecordTypes] = useState<RecordTypeEntry[]>([]);
  const [loadOrderSummary, setLoadOrderSummary] = useState<LoadOrderSummary | null>(null);

  const refreshNavigator = async () => {
    const b = window.chrome?.webview?.hostObjects?.backend;
    if (!b) return;
    try {
      const [typesJson, summaryJson] = await Promise.all([
        b.GetRecordTypeIndex(), b.GetLoadOrderSummary(),
      ]);
      setRecordTypes(JSON.parse(typesJson));
      setLoadOrderSummary(JSON.parse(summaryJson));
    } catch (e) {
      setErrorStr(`Load-order summary failed: ${e}`);
    }
  };

  const refreshMo2Status = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop as
      | { GetMo2Status?: () => Promise<unknown> }
      | undefined;
    if (!b?.GetMo2Status) return;
    try {
      setMo2Status(parseMo2Status(await b.GetMo2Status()));
    } catch {
      setMo2Status(null);
    }
  };

  const refreshPlugins = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    const json = await b.GetPlugins();
    setPlugins(JSON.parse(json));
    setErrorStr("");

    await refreshNavigator();
    await refreshMo2Status();
  };

  useEffect(() => {
    const init = async () => {
      try {
        if (window.chrome?.webview?.hostObjects?.appInterop) {
          const b = window.chrome.webview.hostObjects.appInterop;
          setBackend(() => b);
          await refreshPlugins();
        }
      } catch (err) {
        console.error("Init Error:", err);
        setErrorStr(errorMessage(err) ?? (JSON.stringify(err) || String(err)));
      }
    };
    init();
  }, []);

  useEffect(() => {
    let cancelled = false;
    const tick = async () => {
      const b = window.chrome?.webview?.hostObjects?.appInterop as
        | { GetMo2Status?: () => Promise<unknown> }
        | undefined;
      if (!b?.GetMo2Status) return;
      try {
        const next = parseMo2Status(await b.GetMo2Status());
        if (!cancelled) setMo2Status(next);
      } catch {
        if (!cancelled) setMo2Status(null);
      }
    };
    void tick();
    const id = window.setInterval(tick, 2500);
    return () => {
      cancelled = true;
      window.clearInterval(id);
    };
  }, [backend]);

  const isPendingReloadError = (err: unknown) =>
    String((err as { message?: string })?.message ?? err ?? '').includes('PluginHasPendingChanges');

  const confirmDiscardPendingReload = async () => askConfirm({
    title: 'Discard pending edits?',
    message: 'Reloading the MO2 profile replaces every loaded plugin from disk and will discard unsaved edits. Save those plugins first, or discard them to continue.',
    confirmLabel: 'Discard and reload',
    cancelLabel: 'Cancel',
    danger: true,
  });

  const handleLoadEnv = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    setEnvLoading(true);
    try {
      try {
        await b.LoadEnvironment();
      } catch (err) {
        if (!isPendingReloadError(err)) throw err;
        if (!(await confirmDiscardPendingReload())) return;
        await b.LoadEnvironment(true);
      }
      await refreshPlugins();
    } catch (err) {
      setErrorStr(errorMessage(err) ?? JSON.stringify(err));
    } finally {
      setProgress(null);
      setEnvLoading(false);
    }
  };

  const handleLoadMo2 = async () => {
    const b = window.chrome?.webview?.hostObjects?.appInterop;
    if (!b) return;
    try {
      const path = await b.BrowseForMo2Folder();
      if (!path) return;
      setEnvLoading(true);
      try {
        await b.OpenMo2Profile(path);
      } catch (err) {
        if (!isPendingReloadError(err)) throw err;
        if (!(await confirmDiscardPendingReload())) return;
        await b.OpenMo2Profile(path, true);
      }
      await refreshPlugins();
    } catch (err) {
      setErrorStr(errorMessage(err) ?? JSON.stringify(err));
    } finally {
      setProgress(null);
      setEnvLoading(false);
    }
  };

  return {
    plugins, setPlugins, backend, envLoading, mo2Status, recordTypes, loadOrderSummary,
    refreshNavigator, refreshPlugins, handleLoadEnv, handleLoadMo2,
  };
}
