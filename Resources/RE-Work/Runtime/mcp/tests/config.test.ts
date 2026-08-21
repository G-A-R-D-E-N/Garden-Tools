import { describe, expect, it } from "vitest";
import { loadConfig } from "../src/config.js";

describe("loadConfig", () => {
  it("keeps 1.10.163 as the name authority while decompiling either target build", () => {
    const config = loadConfig({});
    expect(config.defaultVersion).toBe("1.10.163");
    expect(config.versions).toEqual(["1.10.163", "1.11.221"]);
    expect(config.ghidraProjects["1.10.163"].projectName).toBe("Fallout4_163_unpacked");
    expect(config.ghidraProjects["1.11.221"].projectName).toBe("Fallout4_1_11_221_unpacked");
  });

  it("maps every CommonLibF4 runtime slot to a concrete build, in enum order", () => {
    const config = loadConfig({});
    expect(config.runtimeVersions).toEqual({
      kOG: "1.10.163",
      kNG: "1.10.984",
      kAE: "1.11.221",
    });
    // Every slot needs an indexed Address Library or its references cannot be checked at all.
    for (const version of Object.values(config.runtimeVersions)) {
      expect(version).toMatch(/^\d+\.\d+\.\d+$/);
    }
  });

  it("wires a Ghidra project for every version it claims it can decompile", () => {
    const config = loadConfig({});
    for (const version of config.versions) {
      expect(config.ghidraProjects[version]).toBeDefined();
    }
  });

  // A project can hold several programs — 1.11.221 holds the target plus the 1.11.191 AutoVT
  // source import. Without a named program the decompiler picks whichever Ghidra lists first.
  it("names the program to decompile in every Ghidra project", () => {
    const config = loadConfig({});
    for (const version of config.versions) {
      expect(config.ghidraProjects[version].programName).toBe("Fallout4.exe.unpacked.exe");
    }
  });

  it("uses qwen2.5-coder:7b on localhost by default", () => {
    const config = loadConfig({});
    expect(config.ollamaBaseUrl).toBe("http://localhost:11434");
    expect(config.ollamaModel).toBe("qwen2.5-coder:7b");
  });

  it("has bounded, positive resource limits", () => {
    const config = loadConfig({});
    for (const key of [
      "ghidraTimeoutMs",
      "ollamaTimeoutMs",
      "maxDecompileBytes",
      "maxModelResponseBytes",
      "maxChildStderrBytes",
    ] as const) {
      expect(config[key]).toBeGreaterThan(0);
    }
  });

  it("splits RE_MCP_WORKSPACE_ROOTS on the path delimiter", () => {
    const config = loadConfig({ RE_MCP_WORKSPACE_ROOTS: "/a/one:/b/two" });
    expect(config.workspaceRoots).toEqual(["/a/one", "/b/two"]);
  });

  it("rejects a non-numeric timeout override instead of silently using NaN", () => {
    expect(() => loadConfig({ RE_MCP_GHIDRA_TIMEOUT_MS: "soon" })).toThrow(
      /RE_MCP_GHIDRA_TIMEOUT_MS/
    );
  });
});
