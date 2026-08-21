import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    include: ["tests/**/*.test.ts"],
    environment: "node",
    testTimeout: 15_000,
    // The learning DB tests open real SQLite files in temp dirs; running files in
    // parallel is fine, but threads within a file would share those handles.
    fileParallelism: true,
    pool: "forks",
  },
});
