// Message text from anything a host call can reject with: an Error, a plain
// object carrying `message`, or a bare value. Undefined when there is none, so
// callers keep their own fallback.
export function errorMessage(error: unknown): string | undefined {
  if (typeof error !== 'object' || error === null || !('message' in error)) return undefined;
  const { message } = error as { message: unknown };
  return message ? String(message) : undefined;
}

export const errorText = (error: unknown): string => errorMessage(error) ?? String(error);
