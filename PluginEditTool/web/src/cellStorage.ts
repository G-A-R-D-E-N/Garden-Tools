export const LS = (k: string, d: string): string => {
  try { return localStorage.getItem('cell.' + k) ?? d; } catch { return d; }
};

export const setLS = (k: string, v: string): void => {
  try { localStorage.setItem('cell.' + k, v); } catch { return; }
};
