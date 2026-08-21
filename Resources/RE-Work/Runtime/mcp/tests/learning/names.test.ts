import { afterEach, describe, expect, it } from "vitest";
import { bridgeAeIdToOgId, bridgeOgIdToAeId, resolveName } from "../../src/learning/names.js";
import { seedIdSpaces, seededRva, tempStaticDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const OG = "1.10.163";
const AE = "1.11.221";

// og_ae_map's ae_rva column comes from a different disassembly pass than the Address Library and
// sits one entry low against it. Every fixture here reproduces that skew on purpose: matching on
// ae_rva names the previous function, which is wrong in the worst way — confident and plausible.
function db() {
  const d = tempStaticDb();
  seedIdSpaces(d, { ogMax: 2000, aeStride: 20 });
  cleanups.push(() => d.close());

  const ogRva = seededRva(OG, 340);
  const trueAeRva = seededRva(AE, 100007);
  const previousAeRva = seededRva(AE, 100006);

  d.exec(`
    INSERT INTO symbols VALUES ('${OG}', ${ogRva}, 'bhkWorld::RemoveObjects', 'sig', 8);
    INSERT INTO symbols VALUES ('${OG}', ${seededRva(OG, 360)}, 'bhkWorld::SetMotion', 'sig', 8);
    INSERT INTO pdb_symbols (name, signature) VALUES ('bhkWorld::RemoveObjects', 'void RemoveObjects(NiAVObject*)');
    INSERT INTO subsystem_index VALUES ('${OG}', 'Havok_Physics', ${ogRva}, 'bhkWorld::RemoveObjects');
    -- ae_rva deliberately points at the PREVIOUS entry, as the real corpus does.
    INSERT INTO og_ae_map VALUES ('bhkWorld::RemoveObjects(NiAVObject *)', ${ogRva}, 340, ${previousAeRva}, 100007);
    INSERT INTO og_ae_map VALUES ('bhkWorld::SetMotion(NiAVObject *)', ${seededRva(OG, 360)}, 360, ${trueAeRva}, 100008);
  `);
  return d;
}

describe("resolveName", () => {
  it("names an OG address from the curated table", () => {
    const n = resolveName(db(), OG, seededRva(OG, 340), OG, AE);
    expect(n.name).toBe("bhkWorld::RemoveObjects");
    expect(n.source).toBe("symbols");
    expect(n.pdbSignature).toBe("void RemoveObjects(NiAVObject*)");
    expect(n.subsystem).toBe("Havok_Physics");
  });

  it("bridges an AE address by Address Library id, not by og_ae_map.ae_rva", () => {
    // ae_rva for id 100007 in the fixture points one entry low. Matching on it would return
    // SetMotion; the id path returns the function actually at this address.
    const n = resolveName(db(), AE, seededRva(AE, 100007), OG, AE);
    expect(n.bridgeName).toBe("bhkWorld::RemoveObjects(NiAVObject *)");
    expect(n.name).toBe("bhkWorld::RemoveObjects");
    expect(n.ogRva).toBe(seededRva(OG, 340));
  });

  it("returns nothing for an AE address the bridge does not carry", () => {
    const n = resolveName(db(), AE, seededRva(AE, 100200), OG, AE);
    expect(n.name).toBeNull();
    expect(n.source).toBeNull();
  });

  it("has no name path for kNG and says so rather than guessing from OG", () => {
    // The same numeric RVA is a real OG function; an NG lookup must not borrow that answer.
    const n = resolveName(db(), "1.10.984", seededRva(OG, 340), OG, AE);
    expect(n.name).toBeNull();
  });
});

describe("id bridging", () => {
  it("pairs an OG id with its AE id", () => {
    expect(bridgeOgIdToAeId(db(), 340)).toBe(100007);
  });

  it("pairs an AE id back to its OG id", () => {
    expect(bridgeAeIdToOgId(db(), 100007)).toBe(340);
  });

  it("returns null when the bridge has no pairing rather than a near miss", () => {
    expect(bridgeOgIdToAeId(db(), 999)).toBeNull();
  });
});
