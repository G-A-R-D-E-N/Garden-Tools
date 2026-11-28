import { useCallback, useRef } from 'react';

export function useSuiteGate() {
  const idRef = useRef('');
  const stopRef = useRef(false);
  const seqRef = useRef(0);

  const begin = useCallback((): string => {
    if (idRef.current) return '';
    const id = `suite-${++seqRef.current}`;
    idRef.current = id;
    stopRef.current = false;
    return id;
  }, []);
  const end = useCallback(() => {
    idRef.current = '';
    stopRef.current = false;
  }, []);
  const currentId = useCallback(() => idRef.current, []);
  const stopped = useCallback(() => stopRef.current, []);
  const requestStop = useCallback(() => { stopRef.current = true; }, []);

  return { begin, end, currentId, stopped, requestStop };
}
