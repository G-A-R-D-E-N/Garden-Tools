import { CELL_POPOUT_CHANNEL } from './cellPopoutProtocol';

export interface CellPopoutLink {
  post: (message: unknown) => void;
  close: () => void;
}

const inert: CellPopoutLink = { post: () => undefined, close: () => undefined };

export function openCellPopoutLink(onMessage: (value: unknown) => void): CellPopoutLink {
  if (typeof BroadcastChannel === 'undefined') return inert;
  const channel = new BroadcastChannel(CELL_POPOUT_CHANNEL);
  let open = true;
  channel.onmessage = (event: MessageEvent) => {
    if (open) onMessage(event.data);
  };
  return {
    post: message => {
      if (open) channel.postMessage(message);
    },
    close: () => {
      open = false;
      channel.close();
    },
  };
}
