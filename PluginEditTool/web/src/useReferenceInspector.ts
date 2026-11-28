import { useCallback, useState } from 'react';
import { clampInspectorWidth, loadInspectorWidth, saveInspectorWidth, type InspectorTab } from './util/referenceInspector';

export function useReferenceInspector(viewportWidth: number) {
  const [open, setOpen] = useState(false);
  const [tab, setTab] = useState<InspectorTab>('reference');
  const [widthPref, setWidthPref] = useState(() => loadInspectorWidth());
  const [focusNonce, setFocusNonce] = useState(0);
  const show = useCallback((next: InspectorTab) => {
    setTab(next);
    setOpen(true);
    setFocusNonce(n => n + 1);
  }, []);
  const close = useCallback(() => setOpen(false), []);
  return {
    open,
    tab,
    focusNonce,
    show,
    close,
    setTab,
    width: clampInspectorWidth(widthPref, viewportWidth),
    setWidth: setWidthPref,
    commitWidth: saveInspectorWidth,
  };
}

export type ReferenceInspectorState = ReturnType<typeof useReferenceInspector>;
