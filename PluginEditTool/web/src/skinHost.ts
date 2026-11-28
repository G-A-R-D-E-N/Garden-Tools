// Invariant: the skin RPCs ride the NifHost proxy like every other NIF call, so an augmentation adds them.
declare module './backend' {
  interface NifHost {
    SkinInfluences(nifPath: string, block: number, vertex: number): Promise<string>;
    SkinInfluencedBy(nifPath: string, block: number, bone: number, minimum?: number | null): Promise<string>;
    SkinValidate(nifPath: string, block: number): Promise<string>;
    SkinBinding(nifPath: string, block: number | null, skeletonPath: string, skeletonNifPath: string): Promise<string>;
    SkinEdit(nifPath: string, outNif: string, kind: string, block: number, vertex: number | null,
             influences: { bone: number; weight: number }[] | null, fromBone: number | null,
             toBone: number | null, keepSource: boolean, lane: number, fromPositive: boolean,
             overwrite: boolean, expectedSource?: string): Promise<string>;
    SkinToolOp(nifPath: string, outNif: string, op: string, argsJson: string, overwrite: boolean,
               expectedSource?: string): Promise<string>;
  }
}

export {};
