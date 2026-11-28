import { defineConfig } from 'vitest/config';




export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.ts', 'src/**/*.test.tsx'],


    globals: false,
    setupFiles: ['src/testSetup.ts'],
    testTimeout: 20000,
  },
});
