import { useCallback, useEffect, useRef, useState } from 'react';
import type { CellLightingData, CellPlacedReference } from '../backend';
import type { BaseObjectDragPayload } from '../assetBrowser/dragPayload';
import type { Point3 } from '../assetBrowser/placement';
import { openCellPopoutLink, type CellPopoutLink } from './cellPopoutChannel';
import { isCellPopoutIntent, subscribePopoutWindow } from './cellPopoutProtocol';
import { poppedAfterWindowEvent } from './popoutHostState';

export type CellPopoutSource = {
  references: CellPlacedReference[];
  lighting: CellLightingData | null;
  hiddenTypes: ReadonlySet<string>;
  hiddenRefs: ReadonlySet<string>;
  selectedKey: string | null;
};

export type CellPopoutActions = {
  select: (ref: CellPlacedReference | null) => void;
  hide: (ref: CellPlacedReference) => void;
  unhideAll: () => void;
  toggleMarkers: () => void;
  remove: (ref: CellPlacedReference) => void;
  duplicate: (ref: CellPlacedReference) => void;
  details: (ref: CellPlacedReference) => void;
  move: (ref: CellPlacedReference) => void;
  drop: (payload: BaseObjectDragPayload, point: Point3) => void;
  log: (line: string) => void;
};

export function useCellPopoutHost(source: CellPopoutSource, actions: CellPopoutActions) {
  const [popped, setPopped] = useState(false);
  const latest = useRef({ source, actions });
  const link = useRef<CellPopoutLink | null>(null);
  const { references, lighting, hiddenTypes, hiddenRefs, selectedKey } = source;

  useEffect(() => {
    latest.current = { source, actions };
  });

  useEffect(() => {
    const channel = openCellPopoutLink(value => {
      if (!isCellPopoutIntent(value)) return;
      const current = latest.current;
      const byKey = (formKey: string) => current.source.references.find(r => r.formKey === formKey) ?? null;
      switch (value.kind) {
        case 'hello':
          setPopped(true);
          link.current?.post({ kind: 'references', references: current.source.references, lighting: current.source.lighting });
          link.current?.post({
            kind: 'sync',
            hiddenTypes: [...current.source.hiddenTypes],
            hiddenRefs: [...current.source.hiddenRefs],
            selectedKey: current.source.selectedKey,
          });
          return;
        case 'bye':
          setPopped(false);
          return;
        case 'select':
          current.actions.select(value.formKey ? byKey(value.formKey) : null);
          return;
        case 'hide': {
          const ref = byKey(value.formKey);
          if (ref) current.actions.hide(ref);
          return;
        }
        case 'unhideAll':
          current.actions.unhideAll();
          return;
        case 'toggleMarkers':
          current.actions.toggleMarkers();
          return;
        case 'delete': {
          const ref = byKey(value.formKey);
          if (ref) current.actions.remove(ref);
          return;
        }
        case 'duplicate': {
          const ref = byKey(value.formKey);
          if (ref) current.actions.duplicate(ref);
          return;
        }
        case 'details': {
          const ref = byKey(value.formKey);
          if (ref) current.actions.details(ref);
          return;
        }
        case 'move':
          current.actions.move(value.reference);
          return;
        case 'drop':
          current.actions.drop(value.payload, value.point);
          return;
        case 'log':
          current.actions.log(value.line);
          return;
      }
    });
    link.current = channel;
    const offWindow = subscribePopoutWindow(event => setPopped(prev => poppedAfterWindowEvent(prev, event)));
    return () => {
      offWindow();
      channel.post({ kind: 'close' });
      channel.close();
      link.current = null;
    };
  }, []);

  useEffect(() => {
    if (popped) link.current?.post({ kind: 'references', references, lighting });
  }, [popped, references, lighting]);

  useEffect(() => {
    if (popped) {
      link.current?.post({ kind: 'sync', hiddenTypes: [...hiddenTypes], hiddenRefs: [...hiddenRefs], selectedKey });
    }
  }, [popped, hiddenTypes, hiddenRefs, selectedKey]);

  const dockBack = useCallback(() => {
    link.current?.post({ kind: 'close' });
  }, []);

  return { popped, dockBack };
}
