import type { Vector3 } from 'three';
import type { GhostController } from './util/ghostMesh';
import { loadSnapSetting } from './assetBrowser/browserPrefs';
import { snapPoint, type Point3 } from './assetBrowser/placement';
import { BASE_OBJECT_MIME, decodeDragPayload, getActiveDragPayload, type BaseObjectDragPayload } from './assetBrowser/dragPayload';
import { DRAG_OVER_LINE, DROP_REJECTED_LINE, POINT_MISSING_LINE, dropPointLine, dropTypesLine } from './dropDiagnostics';

export type DropSink = {
  placeDrop?: (payload: BaseObjectDragPayload, point: Point3) => void;
  log?: (line: string) => void;
};

export type DropTargetDeps = {
  mount: HTMLElement;
  ghost: GhostController;
  pointAt: (clientX: number, clientY: number) => Point3 | null;
  toScene: (p: Point3) => Vector3;
  sink: () => DropSink;
};

export const isBaseObjectDrag = (e: DragEvent): boolean =>
  !!e.dataTransfer && Array.from(e.dataTransfer.types).includes(BASE_OBJECT_MIME);

export function attachDropTarget(deps: DropTargetDeps): () => void {
  const { mount, ghost, pointAt, toScene, sink } = deps;
  const log = (line: string) => sink().log?.(line);
  const capture = { capture: true };
  let snapGrid = loadSnapSetting();
  let overLogged = false;

  const setHint = (on: boolean) => {
    mount.style.outline = on ? '2px dashed #59d0ff' : '';
    mount.style.outlineOffset = on ? '-4px' : '';
  };

  const onDragEnter = (e: DragEvent) => {
    if (!isBaseObjectDrag(e)) return;
    e.preventDefault();
    snapGrid = loadSnapSetting();
  };

  const onDragOver = (e: DragEvent) => {
    if (!isBaseObjectDrag(e)) return;
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
    setHint(true);
    if (!overLogged) {
      overLogged = true;
      log(DRAG_OVER_LINE);
    }
    const p = pointAt(e.clientX, e.clientY);
    if (!p) {
      ghost.hide();
      return;
    }
    const at = toScene(snapPoint(p, snapGrid));
    if (ghost.visible) ghost.moveTo(at);
    else ghost.show(getActiveDragPayload()?.items[0]?.modelPath ?? '', at);
  };

  const onDragLeave = (e: DragEvent) => {
    if (e.relatedTarget instanceof Node && mount.contains(e.relatedTarget)) return;
    overLogged = false;
    setHint(false);
    ghost.hide();
  };

  const onDrop = (e: DragEvent) => {
    setHint(false);
    ghost.hide();
    overLogged = false;
    if (!isBaseObjectDrag(e)) return;
    e.preventDefault();
    log(dropTypesLine(Array.from(e.dataTransfer?.types ?? [])));
    const payload = decodeDragPayload(e.dataTransfer?.getData(BASE_OBJECT_MIME) ?? '');
    if (!payload) {
      log(DROP_REJECTED_LINE);
      return;
    }
    const p = pointAt(e.clientX, e.clientY);
    if (!p) {
      log(POINT_MISSING_LINE);
      return;
    }
    log(dropPointLine(p, snapPoint(p, loadSnapSetting())));
    sink().placeDrop?.(payload, p);
  };

  mount.addEventListener('dragenter', onDragEnter, capture);
  mount.addEventListener('dragover', onDragOver, capture);
  mount.addEventListener('dragleave', onDragLeave, capture);
  mount.addEventListener('drop', onDrop, capture);

  return () => {
    mount.removeEventListener('dragenter', onDragEnter, capture);
    mount.removeEventListener('dragover', onDragOver, capture);
    mount.removeEventListener('dragleave', onDragLeave, capture);
    mount.removeEventListener('drop', onDrop, capture);
    setHint(false);
  };
}
