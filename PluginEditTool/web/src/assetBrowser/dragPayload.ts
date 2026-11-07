import type { BaseObjectItem } from './baseObjects';

export const BASE_OBJECT_MIME = 'application/x-fo4ide-base-object';

export interface BaseObjectDragItem {
  formKey: string;
  signature: string;
  modelPath: string;
  editorId: string;
}

export interface BaseObjectDragPayload {
  items: BaseObjectDragItem[];
}

let activePayload: BaseObjectDragPayload | null = null;

export function makeDragPayload(items: BaseObjectItem[]): BaseObjectDragPayload {
  return {
    items: items.map(item => ({
      formKey: item.formKey,
      signature: item.signature,
      modelPath: item.modelPath,
      editorId: item.editorId,
    })),
  };
}

export function encodeDragPayload(payload: BaseObjectDragPayload): string {
  return JSON.stringify(payload);
}

export function decodeDragPayload(text: string): BaseObjectDragPayload | null {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isRecord(raw) || !Array.isArray(raw.items)) return null;
  const items = raw.items.filter(isDragItem);
  return items.length > 0 ? { items } : null;
}

export function setActiveDragPayload(payload: BaseObjectDragPayload | null): void {
  activePayload = payload;
}

export function getActiveDragPayload(): BaseObjectDragPayload | null {
  return activePayload;
}

function isDragItem(value: unknown): value is BaseObjectDragItem {
  return isRecord(value)
    && typeof value.formKey === 'string'
    && typeof value.signature === 'string'
    && typeof value.modelPath === 'string'
    && typeof value.editorId === 'string';
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}
