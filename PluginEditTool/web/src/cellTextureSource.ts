import { getCell } from './backend';

export async function loadCellTexture(modelPath: string, relTexPath: string): Promise<string> {
  const c = getCell();
  if (!c) return '';
  try { return await c.GetTexture(modelPath, relTexPath); } catch { return ''; }
}
