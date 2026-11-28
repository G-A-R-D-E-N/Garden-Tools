// Invariant: the pose RPCs ride the NifHost proxy like every other NIF call, so an augmentation adds them.
declare module './backend' {
  interface NifHost {
    ReadKeys(nifPath: string, block: number): Promise<string>;
    PoseList(poseLibrary: string, nifPath?: string | null, block?: number | null): Promise<string>;
    BlockOp(nifPath: string, op: string, block: number | null, child: number | null, donorPath: string,
            blocks: number[] | null, outNif: string, expectedSource: string, overwrite: boolean,
            groups: number[][] | null, poseLibrary: string, pose: string, channel: string,
            interpolation: number | null, keys: { time: number; value: number[] }[] | null): Promise<string>;
  }
}

export {};
