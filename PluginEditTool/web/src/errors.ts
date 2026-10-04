export function errorMessage(error: unknown): string | undefined {
  if (typeof error !== 'object' || error === null || !('message' in error)) return undefined;
  const { message } = error as { message: unknown };
  return message ? String(message) : undefined;
}

export const errorText = (error: unknown): string => errorMessage(error) ?? String(error);
