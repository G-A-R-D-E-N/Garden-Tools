import { HashRouter, Routes, Route, Navigate } from 'react-router-dom';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';
import CellPopoutWindow from './popout/CellPopoutWindow';
import { CELL_POPOUT_ROUTE } from './popout/cellPopoutProtocol';

function App() {
  return (
    <DialogProvider>
    <HashRouter>
      <Routes>
        <Route path="/" element={<Navigate to="/main" replace />} />
        <Route path="/main" element={<MainShell />} />
        <Route path={`/popout/${CELL_POPOUT_ROUTE}`} element={<CellPopoutWindow />} />
      </Routes>
    </HashRouter>
    </DialogProvider>
  );
}

export default App;
