












import { useCallback, useRef, useState, type ReactNode } from 'react';
import PluginTargetDialog, { type TargetRequest, type TargetResult } from './PluginTargetDialog';
import PluginCompareDialog, { type CompareRequest } from './PluginCompareDialog';
import './PluginTargetDialog.css';

import { DialogContext, type DialogApi, type ConfirmRequest, type PromptRequest } from './useDialogs';

type Pending =
  | { kind: 'confirm'; req: ConfirmRequest }
  | { kind: 'prompt'; req: PromptRequest }
  | { kind: 'plugin'; req: TargetRequest }
  | { kind: 'compare'; req: CompareRequest };

export function DialogProvider({ children }: { children: ReactNode }) {
  const [pending, setPending] = useState<Pending | null>(null);
  const resolver = useRef<((v: unknown) => void) | null>(null);

  const open = useCallback(<T,>(p: Pending) => new Promise<T>(resolve => {
    resolver.current = resolve as (v: unknown) => void;
    setPending(p);
  }), []);

  const settle = useCallback((value: unknown) => {
    setPending(null);
    resolver.current?.(value);
    resolver.current = null;
  }, []);

  const api: DialogApi = {
    confirm: req => open<boolean>({ kind: 'confirm', req }),
    prompt: req => open<string | null>({ kind: 'prompt', req }),
    pickPlugin: req => open<TargetResult | null>({ kind: 'plugin', req }),
    compare: req => open<void>({ kind: 'compare', req }),
  };

  return (
    <DialogContext.Provider value={api}>
      {children}
      {pending?.kind === 'confirm' && (
        <ConfirmDialog req={pending.req} onResolve={v => settle(v)} />
      )}
      {pending?.kind === 'prompt' && (
        <PromptDialog req={pending.req} onResolve={v => settle(v)} />
      )}
      {pending?.kind === 'plugin' && (
        <PluginTargetDialog request={pending.req} onResolve={v => settle(v)} />
      )}
      {pending?.kind === 'compare' && (
        <PluginCompareDialog req={pending.req} onClose={() => settle(undefined)} />
      )}
    </DialogContext.Provider>
  );
}

function ConfirmDialog({ req, onResolve }: { req: ConfirmRequest; onResolve: (v: boolean) => void }) {
  return (
    <div className="ptd-overlay" onClick={() => onResolve(false)}>
      <div
        className="ptd-modal glass-panel ptd-modal-sm"
        onClick={e => e.stopPropagation()}
        onKeyDown={e => {
          if (e.key === 'Escape') { e.stopPropagation(); onResolve(false); }
          if (e.key === 'Enter') { e.preventDefault(); onResolve(true); }
        }}
        tabIndex={-1}
        ref={el => el?.focus()}
      >
        <div className="ptd-header"><span className="ptd-title">{req.title}</span></div>
        { }
        <div className="ptd-desc ptd-desc-block">{req.message}</div>
        <div className="ptd-actions">
          <span className="ptd-keys">Enter to confirm, Esc to cancel</span>
          <button className="ptd-btn" onClick={() => onResolve(false)}>{req.cancelLabel ?? 'Cancel'}</button>
          <button
            className={`ptd-btn ${req.danger ? 'ptd-btn-danger' : 'ptd-btn-primary'}`}
            onClick={() => onResolve(true)}
          >
            {req.confirmLabel ?? 'OK'}
          </button>
        </div>
      </div>
    </div>
  );
}

function PromptDialog({ req, onResolve }: { req: PromptRequest; onResolve: (v: string | null) => void }) {
  const [value, setValue] = useState(req.defaultValue ?? '');
  const error = req.validate ? req.validate(value) : null;
  const ok = () => { if (!error) onResolve(value); };
  return (
    <div className="ptd-overlay" onClick={() => onResolve(null)}>
      <div
        className="ptd-modal glass-panel ptd-modal-sm"
        onClick={e => e.stopPropagation()}
        onKeyDown={e => {
          if (e.key === 'Escape') { e.stopPropagation(); onResolve(null); }
          if (e.key === 'Enter' && !error) { e.preventDefault(); ok(); }
        }}
      >
        <div className="ptd-header"><span className="ptd-title">{req.title}</span></div>
        {req.description && <div className="ptd-desc">{req.description}</div>}
        <label className="ptd-extra">
          <span>{req.label}</span>
          <input
            autoFocus
            value={value}
            placeholder={req.placeholder}
            onChange={e => setValue(e.target.value)}
            onFocus={e => e.currentTarget.select()}
          />
        </label>
        {error && <div className="ptd-note ptd-note-warn">{error}</div>}
        <div className="ptd-actions">
          <span className="ptd-keys">Enter to confirm, Esc to cancel</span>
          <button className="ptd-btn" onClick={() => onResolve(null)}>Cancel</button>
          <button className="ptd-btn ptd-btn-primary" onClick={ok} disabled={!!error}>
            {req.confirmLabel ?? 'OK'}
          </button>
        </div>
      </div>
    </div>
  );
}
