export type OcTemplate = { path: string; relative: string; plugin: string; ocId: string };

const normalized = (path: string) => path.replace(/\\/g, '/').toLowerCase();

export const ocTemplateCandidates = (
  templates: OcTemplate[],
  patchPlugin: string,
  outputOc: string,
): OcTemplate[] => {
  const patch = normalized(patchPlugin.trim()).split('/').pop() ?? '';
  const output = normalized(outputOc.trim());
  return templates.filter(template => {
    if (output && normalized(template.path) === output) return false;
    if (patch && template.plugin.toLowerCase() === patch) return false;
    return true;
  });
};
