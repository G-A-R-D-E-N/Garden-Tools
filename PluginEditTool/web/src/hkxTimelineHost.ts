// Invariant: the clip edit RPC rides the HkxHost proxy like every other HKX call, so an augmentation adds it.
// Constraint: the addition stays optional, because older host builds and test doubles carry only the pose RPCs.
declare module './backend' {
  interface HkxHost {
    ClipEdit?(path: string, ops: string, outPath: string, expectedSource: string, overwrite: boolean,
              spline: boolean | null): Promise<string>;
  }
}

export {};
