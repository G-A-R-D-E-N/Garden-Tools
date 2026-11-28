import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const bridgeTarget = process.env.FO4IDE_BRIDGE
const bridgeToken = process.env.FO4IDE_TOKEN ?? ''
const bridgeHeaders = bridgeToken ? { 'X-FO4IDE-Token': bridgeToken } : undefined

export default defineConfig({
  plugins: [react()],
  server: bridgeTarget
    ? {
        proxy: {
          '/rpc': { target: bridgeTarget, changeOrigin: true, headers: bridgeHeaders },
          '/events': { target: bridgeTarget, changeOrigin: true, headers: bridgeHeaders },
        },
      }
    : undefined,
})
