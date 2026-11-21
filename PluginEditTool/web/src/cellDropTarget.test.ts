import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Vector3 } from 'three';
import { attachDropTarget, isBaseObjectDrag, type DropSink } from './cellDropTarget';
import { BASE_OBJECT_MIME, encodeDragPayload, type BaseObjectDragPayload } from './assetBrowser/dragPayload';
import { DRAG_OVER_LINE, DROP_REJECTED_LINE, POINT_MISSING_LINE } from './dropDiagnostics';
import type { GhostController } from './util/ghostMesh';

const payload: BaseObjectDragPayload = {
  items: [{ formKey: '0001A2:Fallout4.esm', signature: 'STAT', modelPath: 'Props/Crate.nif', editorId: 'Crate01' }],
};

function dragEvent(type: string, types: string[], data = ''): DragEvent {
  const event = new Event(type, { bubbles: true, cancelable: true });
  Object.defineProperty(event, 'dataTransfer', {
    value: {
      types,
      dropEffect: 'none',
      getData: (mime: string) => (mime === BASE_OBJECT_MIME ? data : ''),
    },
  });
  return event as DragEvent;
}

function fakeGhost() {
  return { visible: false, show: vi.fn(), moveTo: vi.fn(), hide: vi.fn() };
}

function setup(point: { x: number; y: number; z: number } | null) {
  const mount = document.createElement('div');
  document.body.appendChild(mount);
  const placeDrop = vi.fn<(p: BaseObjectDragPayload, at: { x: number; y: number; z: number }) => void>();
  const log = vi.fn<(line: string) => void>();
  const ghost = fakeGhost();
  const pointAt = vi.fn(() => point);
  const sink = (): DropSink => ({ placeDrop, log });
  const detach = attachDropTarget({
    mount,
    ghost: ghost as unknown as GhostController,
    pointAt,
    toScene: p => new Vector3(p.x, p.y, p.z),
    sink,
  });
  return { mount, placeDrop, log, ghost, detach };
}

describe('cell drop target', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    document.body.innerHTML = '';
  });

  it('recognises a base-object drag by its lower-case MIME type', () => {
    expect(isBaseObjectDrag(dragEvent('dragover', [BASE_OBJECT_MIME]))).toBe(true);
    expect(isBaseObjectDrag(dragEvent('dragover', ['Files']))).toBe(false);
  });

  it('accepts a base-object dragover with the copy drop effect and shows the ghost', () => {
    const { mount, log, ghost } = setup({ x: 1, y: 2, z: 3 });
    const event = dragEvent('dragover', [BASE_OBJECT_MIME]);
    mount.dispatchEvent(event);
    expect(event.defaultPrevented).toBe(true);
    expect(event.dataTransfer?.dropEffect).toBe('copy');
    expect(ghost.show).toHaveBeenCalledTimes(1);
    expect(log).toHaveBeenCalledWith(DRAG_OVER_LINE);
  });

  it('accepts a base-object dragenter so the browser keeps the drop target', () => {
    const { mount } = setup({ x: 1, y: 2, z: 3 });
    const event = dragEvent('dragenter', [BASE_OBJECT_MIME]);
    mount.dispatchEvent(event);
    expect(event.defaultPrevented).toBe(true);
  });

  it('ignores drags that carry no base object', () => {
    const { mount, log } = setup({ x: 1, y: 2, z: 3 });
    const event = dragEvent('dragover', ['Files']);
    mount.dispatchEvent(event);
    expect(event.defaultPrevented).toBe(false);
    expect(log).not.toHaveBeenCalled();
  });

  it('decodes the drop and hands the picked point to placeDrop', () => {
    const { mount, placeDrop, log } = setup({ x: 1, y: 2, z: 3 });
    mount.dispatchEvent(dragEvent('drop', [BASE_OBJECT_MIME], encodeDragPayload(payload)));
    expect(placeDrop).toHaveBeenCalledTimes(1);
    expect(placeDrop).toHaveBeenCalledWith(payload, { x: 1, y: 2, z: 3 });
    expect(log).toHaveBeenCalledWith(`drop received (types=${BASE_OBJECT_MIME})`);
    expect(log).toHaveBeenCalledWith(expect.stringMatching(/^drop point 1\.0,2\.0,3\.0 \(snapped /));
  });

  it('logs a rejected payload and does not place', () => {
    const { mount, placeDrop, log } = setup({ x: 1, y: 2, z: 3 });
    mount.dispatchEvent(dragEvent('drop', [BASE_OBJECT_MIME], 'not json'));
    expect(placeDrop).not.toHaveBeenCalled();
    expect(log).toHaveBeenCalledWith(DROP_REJECTED_LINE);
  });

  it('logs when pointAt finds no point and does not place', () => {
    const { mount, placeDrop, log } = setup(null);
    mount.dispatchEvent(dragEvent('drop', [BASE_OBJECT_MIME], encodeDragPayload(payload)));
    expect(placeDrop).not.toHaveBeenCalled();
    expect(log).toHaveBeenCalledWith(POINT_MISSING_LINE);
  });

  it('stops listening after detach', () => {
    const { mount, log, detach } = setup({ x: 1, y: 2, z: 3 });
    detach();
    const event = dragEvent('dragover', [BASE_OBJECT_MIME]);
    mount.dispatchEvent(event);
    expect(event.defaultPrevented).toBe(false);
    expect(log).not.toHaveBeenCalled();
  });
});
