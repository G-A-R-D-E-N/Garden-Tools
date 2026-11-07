import type { ReactNode } from 'react';
import ReadOnlyBanner from './ReadOnlyBanner';
import { ReadOnlyContext } from './readOnlyContext';
import { useWritableTarget } from './useWritableTarget';

interface Props {
  plugin: string;
  formKey: string;
  onCopied?: (target: string) => void;
  showCopy?: boolean;
  children: ReactNode;
}

export default function WritableScope({ plugin, formKey, onCopied, showCopy = true, children }: Props) {
  const { readOnly, editablePlugins } = useWritableTarget(plugin);
  return (
    <ReadOnlyContext.Provider value={readOnly}>
      {readOnly && (
        <ReadOnlyBanner plugin={plugin} formKey={formKey} editablePlugins={editablePlugins} onCopied={onCopied}
          showCopy={showCopy} />
      )}
      {children}
    </ReadOnlyContext.Provider>
  );
}
