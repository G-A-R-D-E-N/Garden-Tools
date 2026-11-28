// Invariant: the character workspace RPCs ride the HkxHost proxy like every other HKX call, so an augmentation adds them.
// Constraint: the additions stay optional, because older host builds and test doubles do not carry them.
declare module './backend' {
  interface HkxHost {
    CharacterCheck?(skeletonPath: string, ragdollPath: string, mapperPath: string, bodyNifPath: string,
                    skeletonNifPath: string, projectHkx?: string, dataFolder?: string, mo2InstanceDir?: string,
                    mo2Profile?: string): Promise<string>;
    CharacterSavePlan?(skeletonPath: string, skeletonOutPath: string, ragdollPath: string, ragdollOutPath: string,
                       mapperPath: string, mapperOutPath: string, skinPath: string, skinOutPath: string,
                       edits?: string, overwrite?: boolean): Promise<string>;
    CharacterSave?(skeletonPath: string, skeletonOutPath: string, skeletonEdits: string, skeletonExpectedSha: string,
                   ragdollPath: string, ragdollOutPath: string, ragdollEdits: string, ragdollExpectedSha: string,
                   mapperPath: string, mapperOutPath: string, mapperEdits: string, mapperExpectedSha: string,
                   mapperIndex: number, skinPath: string, skinOutPath: string, skinEdits: string,
                   skinExpectedSha: string, overwrite: boolean): Promise<string>;
  }
}

export {};
