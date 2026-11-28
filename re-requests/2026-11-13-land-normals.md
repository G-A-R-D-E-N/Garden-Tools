# RE request: engine behaviour for stale or approximated LAND VNML

Date: 2026-11-13
Requested by: Radicle `edit_land_heights` (PR https://github.com/G-A-R-D-E-N/GardenTools/pull/1252, branch `nomad/cell-exterior-relocate`)
Executable version and hash: unknown. No executable was opened for this request; all facts below come from `Fallout4.esm` plugin data only (size 330,776,576 bytes, SHA-256 `a3fa1e66d75f80f33f864bf471d25b27f48c1cdbb97acb55c4df83b64ccee1d9`).

## What is answered from data (no RE needed)

`edit_land_heights` now rewrites `VNML` (33 x 33 x 3 signed bytes, row-major like `VHGT`, row = y, column = x) from the new float heights it receives, for every vertex whose 3 x 3 height block changed; other vertices keep their shipped bytes. The formula is a measured approximation, not the CK's:

- Per quad `(a, b, c, d)` = (this, east, north, north-east) heights: `gx = ((b - a) + (d - c)) / 256`, `gy = ((c - a) + (d - b)) / 256`, face normal `normalize(-gx, -gy, 1)`; vertex normal is the normalised sum of the four faces around the vertex; byte = `trunc(n * 127)` per axis. Vertex spacing is 128 world units. Border vertices use the neighbour cell's adjacent row or column when its LAND exists, otherwise a one-sided difference.
- Measured on the 17,972 non-flat Commonwealth LAND records of `Fallout4.esm` (19.6 M vertices), angle between the formula and the stored VNML, using the decoded VHGT heights as a stand-in for the CK's unquantised heights: mean 0.81 degrees, median 0.61, p99 5.55, max 132.8, with 0.34 % of vertices above 10 degrees (neighbour heights at borders). Without neighbours (one-sided borders): mean 1.17, p99 10.33. Stale normals after a +200 uniform tilt across a cell (2.8 degrees of slope) are 2.49 degrees off on average (p99 2.80); after a +1000 tilt 12.38 (p99 13.91). Recomputing after the +200 tilt with one-sided borders gives mean 1.22 but p99 10.3, so for a mild edit the border vertices can be worse than stale ones when no neighbour LAND is available.
- Ruled out by the same measurement: central differences (mean 1.09, p99 7.25), 8-neighbour Sobel (0.95, 7.29), `round` instead of `trunc` (0.93), flipped Y or swapped X/Y axes (above 22 degrees). Triangle-fan averages tie the four-face average (0.81, 5.52).
- The residual error is the VHGT quantisation (8-unit steps against 128-unit spacing is up to 3.6 degrees per step), which float input heights avoid; the stored normals still carry detail finer than the VHGT step, so no formula over decoded VHGT is bit-exact.

## What data cannot tell (still open)

1. Exact CK formula, if bit-identical output is ever wanted: kernel, face weighting, border and missing-neighbour handling, and the float-to-byte conversion on the CK's unquantised terrain. Radicle does not need it for correctness; the approximation is within about 1 degree on average.
2. Does the engine read the stored VNML at runtime for terrain lighting, or recompute normals from VHGT on load? If it reads VNML, is a mismatch visible (lighting seams) or harmful (crash, validation failure)? Decides whether the approximation (and the shared-edge normals on neighbouring cells, which Radicle now rewrites from the same approximation for the vertices that read the edited heights) is acceptable in game.
3. Do the LOD terrain generators (xLODGen and the CK LOD build) read VNML or VHGT, so approximate or stale normals leak into generated terrain LOD?
4. When a cell's border heights change, does the CK rewrite the neighbouring cells' VNML border column or row on save, and does the engine treat a seam between differently-edited neighbours specially? (Data only shows that vanilla shared-edge normals agree in 94.9 % of pairs and shared-edge heights in 99.37 %.)

## Answer format needed

For 1: pseudo-code over a height array with the exact constants, with function or address evidence and the engine version it was read from (OG 1.10.163, NG 1.10.984, AE 1.11.240, CK 1.10.162). For 2 to 4: one of "must rewrite", "ignored or rebuilt by the engine", or "misbehaves if stale".

## What the answer unblocks

Whether the approximate VNML (including the neighbour edge heights and normals Radicle now rewrites) can ship as is, or whether the neighbour rewrite is unwanted or must follow the exact CK rule; and whether the formula can be swapped for the exact CK one.
