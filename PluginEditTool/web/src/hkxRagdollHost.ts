// Invariant: the ragdoll save and preview RPCs ride the HkxHost proxy like every other HKX call, so an augmentation adds them.
// Constraint: the additions stay optional, because older host builds and test doubles carry only the read RPCs.
declare module './backend' {
  interface HkxHost {
    SaveRagdoll?(path: string, outPath: string, edits: string, overwrite?: boolean | null,
                 expectedSource?: string | null): Promise<string>;
    PreviewRagdoll?(path: string, options?: string | null): Promise<string>;
  }
}

export {};
