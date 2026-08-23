# 53 - Cell/worldspace child-block grammar (measured)

Foundation for the cell & worldspace completeness focus (see ROADMAP).
All numbers = Fallout4.esm raw GRUP-tree scan.

## 1. GRUP type census

| type | meaning | count |
|---|---|---:|
| 0 | top-level (one per root type) | 140 |
| 1 | worldspace children | 5 |
| 2 / 3 | interior CELL block / sub-block | 10 / 100 |
| 4 / 5 | exterior CELL block / sub-block | 54 / 628 |
| 6 | **cell children** | 38,285 |
| 7 | topic children (INFO under DIAL) | 33,352 |
| 8 | cell persistent block | 533 |
| 9 | cell temporary block | 38,273 |
| 10 | **quest/dialogue children** (DIAL/DLBR/SCEN sets) - *not* a cell block | 1,001 |

Correction to inherited TES5 lore: FO4 masters show **no
visible-distant cell blocks**; the type-10 groups in shipped files
belong to the quest tree.

## 2. Cell-children nesting (canonical)

```
CELL record
└── GRUP t=6  label = cell formID
    ├── GRUP t=8  persistent   label = cell formID   (REFR/ACHR/PHZD)
    └── GRUP t=9  temporary    label = cell formID   (REFR/LAND/NAVM/ACHR/PHZD)
```

* Labels repeat the cell formID at every level.
* World-level persistent data sits in 4 huge type-8 groups (33,930
  records: REFR 33,574 / ACHR 351 / PHZD 5).
* Type-9 groups: 37,091 of them (one per exterior cell), 732,280
  records total.
* **LAND and NAVM live in the temporary block** - a writer that puts
  them anywhere else is wrong.
* Duplicate formIDs occur inside temporary blocks (e.g., three NAVM
  segments sharing one ID); do not treat repetition as corruption.
* Interiors use the same t=6 pattern without the world gating.

## 3. Writer rules

1. Emit t=6 always when a cell has any placed data; label = cell ID.
2. Emit t=8 only for persistent refs; t=9 for everything else incl.
   LAND + NAVM.
3. Never emit a t=10 cell block.
4. Record order inside blocks: free (loader does not require sorted
   formIDs), but keep LAND before NAVM as vanilla does.
