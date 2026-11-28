export const pluginNameOf = (entry: unknown): string =>
  typeof entry === 'string' ? entry : String((entry as { Key?: unknown } | null)?.Key ?? '');
