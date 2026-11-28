import { createContext, useContext } from 'react';
import type { TargetRequest, TargetResult } from './PluginTargetDialog';
import type { CompareRequest } from './PluginCompareDialog';

export interface ConfirmRequest {
  title: string;
  message: string;
  confirmLabel?: string;
  cancelLabel?: string;

  danger?: boolean;
}

export interface PromptRequest {
  title: string;
  label: string;
  defaultValue?: string;
  placeholder?: string;
  description?: string;
  confirmLabel?: string;

  validate?: (value: string) => string | null;
}

export interface DialogApi {
  confirm: (req: ConfirmRequest) => Promise<boolean>;
  prompt: (req: PromptRequest) => Promise<string | null>;
  pickPlugin: (req: TargetRequest) => Promise<TargetResult | null>;
  compare: (req: CompareRequest) => Promise<void>;
}

export const DialogContext = createContext<DialogApi | null>(null);

export function useDialogs(): DialogApi {
  const api = useContext(DialogContext);
  if (!api) throw new Error('useDialogs must be used inside <DialogProvider>');
  return api;
}
