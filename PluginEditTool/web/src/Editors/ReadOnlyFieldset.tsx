import type { ReactNode } from 'react';
import { useReadOnly } from './readOnlyContext';
import './WritableScope.css';

export default function ReadOnlyFieldset({ children }: { children: ReactNode }) {
  const readOnly = useReadOnly();
  return <fieldset className="writable-scope" disabled={readOnly}>{children}</fieldset>;
}
