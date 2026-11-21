// Invariant: the packfile RPC rides the HkxHost proxy like every other HKX call, so an augmentation adds it.
// Constraint: the additions stay optional, because older host builds and test doubles carry only the pose RPCs.
declare module './backend' {
  interface HkxHost {
    InspectObjects?(path: string, classPrefix?: string | null, knownLayoutOnly?: boolean | null,
                   limit?: number | null): Promise<string>;
    RootMotion?(skeletonPath: string, animationPath: string, bone: number,
                limit?: number | null): Promise<string>;
    InspectBehavior?(path: string, limit?: number | null): Promise<string>;
    ReadBehaviorModel?(path: string, limit?: number | null): Promise<string>;
    ReadMappers?(path: string, recompute?: boolean | null, limit?: number | null): Promise<string>;
    ReadRagdoll?(path: string, limit?: number | null): Promise<string>;
    ProjectIndex?(root: string, query?: string | null, limit?: number | null): Promise<string>;
    SkeletonDocument?(skeletonPath: string): Promise<string>;
    SkeletonEdit?(skeletonPath: string, kind: string, bone: number, name?: string | null,
                 parent?: number | null): Promise<string>;
  }
}

export {};
