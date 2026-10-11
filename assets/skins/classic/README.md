# Compatible skin atlas contract

The renderer expects the following loose atlases. Dimensions are minimum pixel bounds; sprite coordinates are in the source layout headers.

| Atlas | Minimum dimensions |
| --- | --- |
| MAIN.BMP | 275x116 |
| TITLEBAR.BMP | 344x87 |
| CBUTTONS.BMP | 136x36 |
| TEXT.BMP | 155x18 |
| SHUFREP.BMP | 92x85 |
| POSBAR.BMP | 307x10 |
| VOLUME.BMP | 68x433 |
| BALANCE.BMP | 47x433 |
| NUMBERS.BMP | 99x13 |
| PLAYPAUS.BMP | 42x9 |
| MONOSTER.BMP | 56x24 |
| EQMAIN.BMP | 275x315 |
| EQ_EX.BMP | 275x82 |
| PLEDIT.BMP | 280x186 |

PLEDIT.TXT requires a `[Text]` section with `Normal`, `Current`, `NormalBG` and `SelectedBG` hexadecimal colors. `Font` defaults to Arial. Tests currently expect Arial and some Classic palette values. PSIZE.CUR is an optional Playlist resize cursor. VISCOLOR.TXT optionally defines 24 RGB visualization colors; absent/malformed input uses the built-in palette. REGION.TXT and the other historical cursors/unused atlases are not required by the loader.

## Build and runtime

The bundled default Classic skin is included in source checkouts and archives. The application and skin-dependent tests use it automatically. The application prefers adjacent `assets/skins/classic` when an `assets` directory exists beside the executable; otherwise it uses the configured source directory. An optional compatible atlas directory can be selected with `YINAMP_SKIN_DIRECTORY`.

Sprite coordinates are defined in [SkinLayout.h](../../../src/skin/SkinLayout.h), [EqualizerLayout.h](../../../src/skin/EqualizerLayout.h) and [PlaylistLayout.h](../../../src/skin/PlaylistLayout.h). See [Building](../../../docs/BUILDING.md) and [Architecture](../../../docs/ARCHITECTURE.md) for build setup and renderer details.

## License

The bundled default Classic skin is MIT licensed and redistributable. See the authoritative public [third-party notice](../../../THIRD_PARTY_NOTICES.md#bundled-default-classic-skin). Preserve the supplied attribution and copyright information when redistributing it.
