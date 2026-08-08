import './hostbridge.ts'   // must run before anything reads window.chrome.webview
import './navGuard.ts'     // keep the WebView pinned to the SPA (belt-and-suspenders with the server redirect)
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
