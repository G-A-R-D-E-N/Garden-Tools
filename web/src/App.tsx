import { HashRouter, Routes, Route } from 'react-router-dom';
import ConflictResolver from './ConflictResolver';
import MainShell from './MainShell';

function App() {
  return (
    <HashRouter>
      <Routes>
        <Route path="/" element={<ConflictResolver />} />
        <Route path="/main" element={<MainShell />} />
      </Routes>
    </HashRouter>
  );
}

export default App;
