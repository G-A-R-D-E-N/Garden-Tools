// Invariant: the animation index RPC rides the HkxHost proxy like every other HKX call, so an augmentation adds it.
// Constraint: the addition stays optional, because older host builds and test doubles carry only the pose RPCs.
declare module './backend' {
  interface HkxHost {
    AnimationIndex?(
      dataFolder: string, mo2Folder: string, profile: string, kind: string, project: string, race: string,
      character: string, query: string, resolvePath: string, offset: number, limit: number, refresh: boolean,
    ): Promise<string>;
    ExtractAnimationAsset?(dataFolder: string, mo2Folder: string, profile: string, path: string): Promise<string>;
  }
  interface NifHost {
    GeoSkinned?(nifPath: string): Promise<string>;
  }
}

export {};
