import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

// Regression guard: RE_ROOT used to be derived only from the module's own location
// (`join(__dirname, "..", "..")`), which is the bundle's unpack directory when the MCP runs from a
// `.mcpb` — so `search_findings` read a nonexistent `03-FINDINGS.md` and always returned `[]`. The
// fix makes `RE_ROOT` honor `RE_MCP_ROOT`. Because `RE_ROOT` is computed at module load, each test
// points `RE_MCP_ROOT` at a fresh fixture directory and re-imports the tool
// (`vi.resetModules` + dynamic import) so the env var is observed.
describe("searchFindings honors RE_MCP_ROOT", () => {
  // Unique to the fixture so a hit proves the *fixture* directory was read. In the test
  // environment the default RE_ROOT also resolves to the real `ReverseEngineering` docs (which
  // contain "ButtonInfo"), so asserting on "ButtonInfo" alone would pass even without the fix.
  const MARKER = "FIXTURE_ONLY_TERM_a1b2c3d4";

  let dir: string;
  let original: string | undefined;

  beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), "re-findings-"));
    writeFileSync(
      join(dir, "03-FINDINGS.md"),
      `## Fixture\n\nMultiActivateManager::ButtonInfo layout ${MARKER} confirmed.\n`,
    );
    original = process.env.RE_MCP_ROOT;
    process.env.RE_MCP_ROOT = dir;
    vi.resetModules();
  });

  afterEach(() => {
    if (original === undefined) {
      delete process.env.RE_MCP_ROOT;
    } else {
      process.env.RE_MCP_ROOT = original;
    }
    vi.resetModules();
    rmSync(dir, { recursive: true, force: true });
  });

  it("returns matches from the doc under RE_MCP_ROOT", async () => {
    const { searchFindings } = await import("../../src/tools/searchFindings.js");
    const matches = searchFindings("ButtonInfo");
    expect(matches.length).toBeGreaterThan(0);
    expect(matches[0].doc).toBe("03-FINDINGS.md");
  });

  it("reads the fixture directory, not the default RE_ROOT", async () => {
    const { searchFindings } = await import("../../src/tools/searchFindings.js");
    // The marker exists only in the fixture. If RE_MCP_ROOT were ignored, RE_ROOT would point at
    // the real docs and this lookup would come back empty.
    expect(searchFindings(MARKER).length).toBe(1);
  });
});
