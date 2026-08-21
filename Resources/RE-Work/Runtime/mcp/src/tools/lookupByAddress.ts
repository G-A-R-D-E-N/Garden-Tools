import { formatVa, getDb, parseAddressToRva } from "../db.js";

export interface AddressLookupResult {
  version: string;
  rva: string;
  va: string;
  name: string;
  signature: string;
  sizeof: number | null;
  addressLibraryId: number | null;
}

export function lookupByAddress(address: string, version = "1.10.163"): AddressLookupResult[] {
  const rva = parseAddressToRva(address);
  if (rva === null) {
    throw new Error(`Could not parse "${address}" as a hex address.`);
  }

  const db = getDb();
  const symbolRows = db
    .prepare("SELECT rva, name, signature, sizeof FROM symbols WHERE version = ? AND rva = ?")
    .all(version, rva) as { rva: number; name: string; signature: string; sizeof: number | null }[];

  const idRow = db
    .prepare("SELECT id FROM address_library WHERE version = ? AND rva = ?")
    .get(version, rva) as { id: number } | undefined;

  if (symbolRows.length === 0) {
    return [
      {
        version,
        rva: "0x" + rva.toString(16),
        va: formatVa(rva),
        name: "(no symbol found at this address)",
        signature: "",
        sizeof: null,
        addressLibraryId: idRow?.id ?? null,
      },
    ];
  }

  return symbolRows.map((row) => ({
    version,
    rva: "0x" + row.rva.toString(16),
    va: formatVa(row.rva),
    name: row.name,
    signature: row.signature,
    sizeof: row.sizeof,
    addressLibraryId: idRow?.id ?? null,
  }));
}
