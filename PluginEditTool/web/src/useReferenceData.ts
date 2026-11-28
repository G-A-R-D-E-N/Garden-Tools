import { useEffect, useState } from 'react';
import { getBackend, getCell, type PluginMatrixRow } from './backend';
import type { ReferenceDetails, ReferenceError } from './referenceDetails';
import { errorText } from './errors';
import { hasReferenceScripts } from './util/referenceInspector';

export interface ReferenceData {
  loading: boolean;
  error: string | null;
  details: ReferenceDetails | null;
  plugins: PluginMatrixRow[];
}

interface LoadedReference {
  key: string;
  details: ReferenceDetails | null;
  error: string | null;
  plugins: PluginMatrixRow[];
}

export function isReferenceError(value: unknown): value is ReferenceError {
  return typeof value === 'object' && value !== null && typeof (value as { error?: unknown }).error === 'string';
}

export async function referenceHasScripts(formKey: string): Promise<boolean> {
  const cell = getCell();
  if (!cell) return false;
  try {
    const parsed: unknown = JSON.parse(await cell.GetReferenceDetails(formKey));
    return !isReferenceError(parsed) && hasReferenceScripts(parsed as ReferenceDetails | null);
  } catch {
    return false;
  }
}

async function loadReference(formKey: string): Promise<Omit<LoadedReference, 'key'>> {
  const backend = getBackend();
  const cell = getCell();
  let details: ReferenceDetails | null = null;
  let error: string | null = null;
  let plugins: PluginMatrixRow[] = [];
  try {
    if (!cell) throw new Error('Cell host unavailable');
    const parsed: unknown = JSON.parse(await cell.GetReferenceDetails(formKey));
    if (isReferenceError(parsed)) error = parsed.error;
    else details = parsed as ReferenceDetails | null;
  } catch (err) {
    error = errorText(err);
  }
  try {
    if (backend?.GetRecordPluginMatrix) {
      const rows: unknown = JSON.parse(await backend.GetRecordPluginMatrix(formKey));
      if (Array.isArray(rows)) plugins = rows as PluginMatrixRow[];
    }
  } catch {
    plugins = [];
  }
  return { details, error, plugins };
}

export function useReferenceData(formKey: string, refRevision: number): ReferenceData {
  const loadKey = `${formKey}#${refRevision}`;
  const [loaded, setLoaded] = useState<LoadedReference | null>(null);

  useEffect(() => {
    let cancelled = false;
    void loadReference(formKey).then((result) => {
      if (!cancelled) setLoaded({ key: loadKey, ...result });
    });
    return () => {
      cancelled = true;
    };
  }, [formKey, loadKey]);

  if (loaded === null || loaded.key !== loadKey) {
    return { loading: true, error: null, details: null, plugins: [] };
  }
  return { loading: false, error: loaded.error, details: loaded.details, plugins: loaded.plugins };
}
