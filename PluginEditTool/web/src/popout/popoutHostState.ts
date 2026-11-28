import { CELL_POPOUT_ROUTE, type PopoutWindowEvent } from './cellPopoutProtocol';

export function poppedAfterWindowEvent(popped: boolean, event: PopoutWindowEvent): boolean {
  if (event.Route !== CELL_POPOUT_ROUTE) return popped;
  return event.Open ? popped : false;
}
