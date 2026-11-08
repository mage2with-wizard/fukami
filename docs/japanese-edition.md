# Japanese edition (SLPS-20001) — experimental port

This branch runs **Ridge Racer V (Japan), SLPS-20001** on Fukami. It is a working experiment, kept in its own
folders next to the North American code rather than merged into `src/`:

| Folder | What it holds |
|---|---|
| `jp-experiment/` | JP versions of the product sources (`main_jp.cpp`, `patches_jp.cpp`, `stubs_jp.cpp`, `app_linux_jp.cpp`, `native_jp.inc`, `rr5_car_builder_jp.h`), the build (`build.cmake`), the launcher, default settings and test scripts/workloads |
| `jp-analysis/` | PS2Recomp configurations for the JP executable (`regen.toml` is the one used for the build) |
| `jp-tools/` | The scripts that mapped North American addresses to Japanese ones (text-section matching, GP-relative data, config translation) and the disc file table (`files.json`: names, LBAs, sizes, SHA-256) |

As in the rest of the project, **no game code or game data is in the repository**: the recompiled code
(`generated/`), the native hot functions (`jp-experiment/native/rrv_ee_native.inc`), the extracted disc files and
all build output are generated on your computer from your own disc and are ignored by git.

## Status (2026-11-08)

Verified on the Japanese edition, mostly by replaying recorded play with fixed input (`RRV_GATE3_BOUND_WORKLOAD`):

- Boot, menus, settings, memory card save and load (JP `sceMcGetInfo` takes 4 arguments: `patch_0x2c8708_mc_getinfo4`)
- A full 4-round Grand Prix: NEXT Rd. progression, results, trophy, status report; the replayed card is
  byte-identical to the card from the live session
- Speed-ups: 29 native hot functions, car vertex builder, split GS, fast unpack with read-ahead (live 4-round GP:
  33 late fields out of 76,407)
- Real widescreen 16:9 / 21:9 with `hud = race`, car LOD (default 4), draw distance, car depth fix
- VS mode reaches the split-screen race with scripted input on two ports (not tested with two real pads)

Fix also useful upstream: `Ps2VarArgCursor::nextU64` (ps2xRuntime `Support.h`) read a `double` vararg as an O32
register pair; the EE passes it in one 64-bit GPR (results screen showed 0.000 km/h).

## Known limitation: this branch is JP-only

Two shared files carry hard-coded GP-relative offsets that differ between the editions, and this branch switches them
to the Japanese values: `src/hle/spu2.cpp` (`gp - 0x6320` → `gp - 0x6350`) and the ps2xRuntime `Interrupt.cpp`
SyncV wait (`gp - 0x5c80` → the JP offset). **The North American build is not correct on this branch.** Merging
upstream would need these to come from the edition profile instead of constants.

## Building (outline)

The scripts still carry the absolute paths of the machine they were written on (`/tmp/fukami-region-analysis`,
`/tmp/fukami-jp-source`); adjust them before use.

1. Extract the JP files from your own disc image: `python3 jp-tools/extract_jp.py "<your Ridge Racer V (Japan).bin>"`
   (checks every file against `jp-tools/files.json`).
2. Generate the game code: run PS2Recomp (`ps2xRecomp/ps2_recomp`) on `jp-analysis/regen.toml` and copy its output to
   `generated/rr5/output/`.
3. Generate the native hot functions with `tools/ee-native/ee_native_gen.py` from that output into
   `jp-experiment/native/` (the function list is in `jp-experiment/native/manifest.json`).
4. Configure with `-DRRV_JP_EXPERIMENT=ON` (see the cache variables at the top of `jp-experiment/build.cmake`)
   and build; `jp-experiment/package-jp-app.sh` assembles the app folder.
