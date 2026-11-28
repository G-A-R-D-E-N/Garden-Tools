import { useState, type ComponentProps } from 'react';
import { ExternalLink, PanelTopClose } from 'lucide-react';
import CellViewport from './CellViewport';
import { buildCellPopoutUrl, CELL_POPOUT_WINDOW_NAME } from './popout/cellPopoutProtocol';
import { currentHostToken } from './popout/popoutHostToken';
import './CellViewportStage.css';

type CellViewportStageProps = ComponentProps<typeof CellViewport> & {
  popped: boolean;
  onDockBack: () => void;
};

export default function CellViewportStage({ popped, onDockBack, ...viewport }: CellViewportStageProps) {
  const [popError, setPopError] = useState('');

  const popOut = () => {
    try {
      window.open(buildCellPopoutUrl(window.location, currentHostToken()), CELL_POPOUT_WINDOW_NAME);
      setPopError('');
    } catch (e) {
      setPopError('Could not open the pop-out window: ' + (e instanceof Error ? e.message : String(e)));
    }
  };

  if (popped) {
    return (
      <div className="cell-view-stage cell-view-popped">
        <div className="nif-view-empty">
          <span>The viewport is open in its own window.</span>
          <button className="papyrus-copy" onClick={onDockBack}><PanelTopClose size={13} /> Dock back</button>
        </div>
      </div>
    );
  }

  return (
    <div className="cell-view-stage">
      <CellViewport {...viewport} />
      <button className="papyrus-copy cell-popout-btn" onClick={popOut} title="Open the viewport in its own window">
        <ExternalLink size={13} /> Pop out
      </button>
      {popError && <div className="cell-popout-error">{popError}</div>}
      {viewport.references.length === 0 && <div className="nif-view-empty cell-view-overlay">This cell has no placed references.</div>}
    </div>
  );
}
