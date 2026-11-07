export function mo2FreshnessLabel(args: {
  loaded?: boolean;
  stale?: boolean;
  staleConfig?: boolean;
  staleLoadOrder?: boolean;
  profile?: string;
}): string | null {
  if (!args.loaded) return null;
  const name = args.profile ? `MO2 ${args.profile}` : 'MO2 profile';
  if (!args.stale) return `${name} current`;
  if (args.staleConfig && !args.staleLoadOrder) return `${name} config changed`;
  if (args.staleLoadOrder && !args.staleConfig) return `${name} load order changed`;
  return `${name} changed`;
}
