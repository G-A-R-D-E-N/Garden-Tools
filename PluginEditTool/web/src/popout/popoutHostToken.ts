const HOST_TOKEN_KEY = 'fo4ide.token';

export function currentHostToken(): string {
  try {
    return sessionStorage.getItem(HOST_TOKEN_KEY) ?? '';
  } catch {
    return '';
  }
}
