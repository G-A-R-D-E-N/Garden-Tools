import type { BrowserColumn } from './browserPrefs';

export const ROW_HEIGHT = 24;
export const HEADER_HEIGHT = 26;

export const COLUMN_LABELS: Record<BrowserColumn, string> = {
  editorId: 'Editor ID',
  hexId: 'Form ID',
  name: 'Name',
  type: 'Type',
  modelPath: 'Model path',
  plugin: 'Plugin',
};

export const INDEXING_MESSAGE = 'Indexing objects (first run takes up to ~30 s)\u2026';
export const NO_MATCH_MESSAGE = 'No objects match the filter.';
