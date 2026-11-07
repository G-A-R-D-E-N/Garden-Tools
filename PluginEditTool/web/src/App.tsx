import { HashRouter, Routes, Route, Navigate } from 'react-router-dom';
import { DialogProvider } from './dialogs';
import MainShell from './MainShell';

function App() {
  return (
    <DialogProvider>
    <HashRouter>
      <Routes>
        <Route path="/" element={<Navigate to="/main" replace />} />
        <Route path="/main" element={<MainShell />} />
      </Routes>
    </HashRouter>
    </DialogProvider>
  );
}

export default App;
