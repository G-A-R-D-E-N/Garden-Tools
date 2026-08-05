// Navigator filter shape, in its own module so NavigatorFilters.tsx exports only a component
// (a file that mixes components and constants loses fast refresh).

export interface NavigatorFilterState {
  /** Record signature to restrict to, or "" for all types. */
  type: string;
  /** FormID / EditorID substring. */
  needle: string;
}

export const EMPTY_FILTERS: NavigatorFilterState = { type: '', needle: '' };
