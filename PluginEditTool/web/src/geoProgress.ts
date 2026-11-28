export type GeoProgress = {
  done: number;
  total: number;
  phase?: 'indexing' | 'converting';
  indexDone?: number;
  indexTotal?: number;
};
