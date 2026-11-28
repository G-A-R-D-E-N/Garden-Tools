// Invariant: the mapper edit RPC rides the HkxHost proxy like every other HKX call, so an augmentation adds it.
// Constraint: the addition stays optional, because older host builds and test doubles carry only the read RPCs.
declare module './backend' {
  interface HkxHost {
    EditMapper?(path: string, mapper: number, edits: string, outPath?: string | null, overwrite?: boolean | null,
                expectedSource?: string | null, limit?: number | null): Promise<string>;
  }
}

export {};
