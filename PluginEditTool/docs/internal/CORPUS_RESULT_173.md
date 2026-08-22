# PR #173 corpus result artifact

Auditable record of the inherited auto-property backing-variable fix (#173) against the
vanilla differential.

> **Read `regressed_count 0` precisely.** It means **no script flipped identical->differing among
> the scripts whose identity changed** across the fix. It does NOT examine scripts that were
> non-identical BOTH before and after, so it is not a proof that nothing worsened inside the
> always-differing set. The load-bearing evidence is the aggregate moving strictly up
> (7182 -> 7242, net +60), which establishes a **net fidelity improvement** only -- see
> `docs/internal/PAPYRUS.md`.
>
> **Reproducibility is partial.** The standard differential test emits aggregate counts plus the
> first 30 differences only; it does NOT emit the full matched/unmatched set, so this exact 60/0
> `fixed`/`regressed` breakdown is not regenerable from the documented command alone. It was
> produced by a one-off per-script set-diff between the base and head runs against the pinned corpus
> B build (buildid `20686350`, hashes below). Reproducing the aggregate figures needs that same
> pinned build; reproducing the per-script lists needs the set-diff step too.

```
base_sha         bd39825 (#172, corpus B baseline)
head_sha         bc48afe (#173 fix)
corpus           B, Fallout 4 depot buildid 20686350
misc_ba2_sha256  a70c58200a31b9ee6491ba88d7be9cf03104a4551c13f7b5b9cb53b8f06b9178
base_zip_sha256  8071238bea2e7172d1bc99d2bf9ee6f7c2696b149824061d884c774b3c897f21
pairs            7571
compiled         7543
refused          28   (unchanged)
identical_before 7182   (95.21%)
identical_after  7242   (96.01%)
net              +60
fixed_count      60
regressed_count  0
```

## fixed_scripts (60)

- CA_DB_Event01.psc
- CA_DB_Event02.psc
- CA_DB_Event03.psc
- CA_DB_Event04.psc
- CA_DB_Event05.psc
- CA_DB_Event06.psc
- CA_DB_Event07.psc
- CA_DB_Event08.psc
- CA_DB_Event09.psc
- CA_DB_Event10.psc
- CA_DB_Event11.psc
- CA_DB_Event12.psc
- CA_DB_Event13.psc
- CA_DB_Event14.psc
- CA_DB_Event15.psc
- CA_DB_Event16.psc
- CA_DB_Event17.psc
- CA_DB_Event18.psc
- CA_DB_Event19.psc
- CA_DB_Event20.psc
- CA_DB_Event21.psc
- CA_DB_Event22.psc
- CA_DB_Event23.psc
- CA_DB_Event24.psc
- CA_DB_Event25.psc
- CA_DB_Event26.psc
- CA_DB_Event27.psc
- CA_DB_Event28.psc
- CA_DB_Event29.psc
- CA_DB_Event30.psc
- CA_DB_Event31.psc
- CA_DB_Event32.psc
- CA_DB_Event33.psc
- CA_DB_Event34.psc
- DN017_AssaultSynthRespawnScriptA.psc
- DN070_SecurityProtectronScript.psc
- DN070_TurretHallIDCardReaderScript.psc
- DN084_GlassElevatorIDCardReaderScript.psc
- DN084_LabIDCardReaderScript.psc
- DefaultRefOnTriggerLeave.psc
- MS17StartSceneTriggerScript.psc
- MinAO_EggTargetDestroyed.psc
- MinDefendCastleScript.psc
- MinRadiantOwned01Script.psc
- MinRadiantOwned04Script.psc
- MinRadiantOwned07Script.psc
- MinRadiantOwnedBuildResourceScript.psc
- MinRecruit08Script.psc
- ModLegendaryConsecutiveHitsScript.psc
- REChokepointTriggerScript.psc
- RESceneKMK10AliasScript.psc
- RRR04Script.psc
- TrapFlamethrower.psc
- TrapGrenadeBouquet.psc
- TrapIED.psc
- TrapRadiationScript.psc
- WorkshopRE01Script.psc
- WorkshopRE03Script.psc
- WorkshopSynthAttackScript.psc
- defaultQuestRespawnScriptB.psc

## regressed_scripts (0)

(none)
