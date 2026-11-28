// Invariant: the sweep RPCs ride the HkxHost proxy like every other HKX call, so an augmentation adds them.
// Constraint: the additions stay optional, because older host builds and test doubles do not carry them.
declare module './backend' {
  interface HkxHost {
    SubgraphSweep?(dataFolder: string, mo2Folder: string, profile: string): Promise<string>;
    SubgraphDiff?(dataFolder: string, mo2Folder: string, profile: string): Promise<string>;
    CrashSubgraph?(logPath: string, text: string, dataFolder: string, mo2Folder: string, profile: string): Promise<string>;
    ModlistScan?(mode: string, dataFolder: string, mo2Folder: string, profile: string): Promise<string>;
  }
}

export {};
