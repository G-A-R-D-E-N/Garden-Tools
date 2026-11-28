// Invariant: the chain RPCs ride the HkxHost proxy like every other HKX call, so an augmentation adds them.
// Constraint: the additions stay optional, because older host builds and test doubles carry only the pose RPCs.
declare module './backend' {
  interface HkxHost {
    ProjectChain?(hkxPath: string, dataFolder: string, mo2Folder: string, profile: string): Promise<string>;
    ProjectCheck?(hkxPath: string, dataFolder: string, mo2Folder: string, profile: string, limit: number,
                  format?: string): Promise<string>;
    SubgraphManifest?(id: string, behaviorPath: string, pathPrefixes: string[] | null, dataFolder: string,
                      mo2Folder: string, profile: string): Promise<string>;
  }
}

export {};
