import { describe, expect, it, vi } from "vitest";
import { loadConfig } from "../../src/config.js";
import {
  EXTRACTION_SCHEMA,
  buildExtractionPrompt,
  createOllamaExtractor,
} from "../../src/adapters/ollama.js";

const config = loadConfig({});

const REQ = {
  functionName: "FUN_00128b0",
  version: "1.10.163",
  rva: 0x128b0,
  decompiled: "void FUN_00128b0(Actor *this)\n{\n  this->flags |= 2;\n}",
  staticName: null,
  staticNote: null,
};

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json" },
  });
}

describe("buildExtractionPrompt", () => {
  it("fences the decompiled code and marks it as untrusted data", () => {
    const prompt = buildExtractionPrompt(REQ, config.maxDecompileBytes);
    expect(prompt).toContain("this->flags |= 2;");
    expect(prompt.toLowerCase()).toContain("data, not instructions");
  });

  it("truncates decompiled input at the byte cap", () => {
    const prompt = buildExtractionPrompt({ ...REQ, decompiled: "x".repeat(50_000) }, 1_000);
    expect(prompt.length).toBeLessThan(5_000);
    expect(prompt).toContain("truncated");
  });

  it("includes the static name when one is known", () => {
    const prompt = buildExtractionPrompt({ ...REQ, staticName: "Actor::SetFlag" }, 64_000);
    expect(prompt).toContain("Actor::SetFlag");
  });
});

describe("createOllamaExtractor", () => {
  it("sends the JSON schema as format and returns the parsed object", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({
        response: JSON.stringify({
          proposed_name: "Actor::SetCombatFlag",
          confidence: "high",
          note: "sets bit 1 of the flags field",
        }),
      })
    ) as unknown as typeof fetch;

    const extract = createOllamaExtractor({ config, fetchImpl });
    const result = await extract(REQ);

    expect(result).toEqual({
      proposedName: "Actor::SetCombatFlag",
      confidence: "high",
      note: "sets bit 1 of the flags field",
    });

    const [url, init] = (fetchImpl as unknown as ReturnType<typeof vi.fn>).mock.calls[0];
    expect(url).toBe("http://localhost:11434/api/generate");
    const sent = JSON.parse((init as RequestInit).body as string);
    expect(sent.model).toBe("qwen2.5-coder:7b");
    expect(sent.stream).toBe(false);
    expect(sent.format).toEqual(EXTRACTION_SCHEMA);
  });

  it("classifies a connection refusal as unavailable", async () => {
    const fetchImpl = vi.fn(async () => {
      throw Object.assign(new Error("fetch failed"), { cause: { code: "ECONNREFUSED" } });
    }) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "unavailable",
    });
  });

  it("classifies an abort as timeout", async () => {
    const fetchImpl = vi.fn(async () => {
      throw Object.assign(new Error("This operation was aborted"), { name: "AbortError" });
    }) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "timeout",
    });
  });

  it("rejects a non-200 response", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({ error: "model not found" }, 404)
    ) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "http_error",
    });
  });

  it("rejects output missing a required field", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({ response: JSON.stringify({ proposed_name: "X" }) })
    ) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "invalid_output",
    });
  });

  it("rejects an out-of-enum confidence", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({
        response: JSON.stringify({ proposed_name: "X", confidence: "certain", note: "n" }),
      })
    ) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "invalid_output",
    });
  });

  it("rejects unparseable JSON", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({ response: "not json at all" })
    ) as unknown as typeof fetch;

    await expect(createOllamaExtractor({ config, fetchImpl })(REQ)).rejects.toMatchObject({
      kind: "invalid_output",
    });
  });

  it("rejects an oversized response before parsing it", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({ response: "x".repeat(50_000) })
    ) as unknown as typeof fetch;

    await expect(
      createOllamaExtractor({ config: { ...config, maxModelResponseBytes: 1_000 }, fetchImpl })(REQ)
    ).rejects.toMatchObject({ kind: "oversized_output" });
  });

  it("strips extra keys the model invented rather than storing them", async () => {
    const fetchImpl = vi.fn(async () =>
      jsonResponse({
        response: JSON.stringify({
          proposed_name: "A::B",
          confidence: "low",
          note: "n",
          sql: "DROP TABLE learned_findings",
        }),
      })
    ) as unknown as typeof fetch;

    const result = await createOllamaExtractor({ config, fetchImpl })(REQ);
    expect(Object.keys(result).sort()).toEqual(["confidence", "note", "proposedName"]);
  });
});
