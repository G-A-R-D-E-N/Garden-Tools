import type { MutableRefObject } from 'react';
import { useExternalCellChanges } from './useExternalCellChanges';

export interface CellExternalSyncArgs {
  cellFormKey: string | null | undefined;
  gizmoPatchRef: MutableRefObject<string | null>;
  busyRef: MutableRefObject<boolean>;
  appendLog: (line: string) => void;
  reloadReferences: (select: string | null, from: string | null) => Promise<unknown>;
  setRefRevision: (update: (revision: number) => number) => void;
  refreshEditCounts: (patch: string) => Promise<unknown>;
}

export function useCellExternalSync(args: CellExternalSyncArgs): void {
  useExternalCellChanges({
    cellFormKey: args.cellFormKey,
    gizmoPatchRef: args.gizmoPatchRef,
    isBusy: () => args.busyRef.current,
    onChange: async event => {
      args.appendLog('External change (MCP): ' + event.Method);
      await args.reloadReferences(null, null);
      args.setRefRevision(r => r + 1);
      await args.refreshEditCounts(args.gizmoPatchRef.current ?? '');
    },
  });
}
