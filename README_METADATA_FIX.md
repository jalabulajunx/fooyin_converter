# Metadata Tag Preservation Fix

## Approach

Unlike GStreamer-based converters (e.g. Gnac) where tags flow through the pipeline automatically, this plugin shells out to CLI encoders directly. None of these encoders have a universal cross-format "copy all tags" flag. The fix therefore uses a two-step approach:

1. **Read tags** from the source file using `ffprobe` (JSON output, `-show_format`)
2. **Pass tags explicitly** to each encoder using its real CLI flags
3. **Extract cover art** from the source file using `ffmpeg` to a temp JPEG, then embed it using each encoder's picture flag

---

## Changes Applied

### `codecwrapper.h`
- Added `QMap<QString, QString> tags` field to `ConversionOptions` — holds all text tags read from the source file
- Added `QString coverArtPath` field to `ConversionOptions` — path to extracted cover art temp file (empty if none or ffmpeg unavailable)

### `converterwidget.cpp`
- Added `readTagsFromFile()` — runs `ffprobe -print_format json -show_format` and parses the `format.tags` object. Normalises known ffprobe key name differences to Vorbis comment conventions:
  - `album_artist` → `ALBUMARTIST`
  - `track` → `TRACKNUMBER`
  - `disc` → `DISCNUMBER`
- Added `extractCoverArt()` — runs `ffmpeg -an -vframes 1` to extract the first video stream (cover art) to `/tmp/fooyin_converter_cover.jpg`. Logs a warning and returns empty string if `ffmpeg` is not found; conversion continues normally without cover art.
- Both functions are called before each `convertAsync` invocation (single file and batch modes)
- Temp cover art file is deleted in `onFinished` after each track completes

### `flacwrapper.cpp`
- Removed invalid flags: `--keep-foreign-metadata-if-fail` (does not exist in flac 1.5.0), `--import-tags-from`, `--import-pictures-from`
- Fixed stderr logging race: `readyReadStandardError` was consuming stderr before the `finished` handler could read it; stderr is now accumulated into a shared buffer
- Tags passed via `--tag=KEY=VALUE` (one argument per tag)
- Cover art passed via `--picture=<path>`

### `lamewrapper.cpp`
- Removed fake flag `--tagcopy all`
- Standard tags mapped to LAME's named flags: `--tt` (title), `--ta` (artist), `--tl` (album), `--ty` (date/year), `--tn` (track), `--tg` (genre), `--tc` (comment)
- Extended tags mapped to ID3v2 frame IDs via `--tv`: `TPE2` (album artist), `TPOS` (disc), `TCOM` (composer), `TCOP` (copyright)
- Cover art passed via `--ti <path>`

### `oggwrapper.cpp`
- Removed fake flag `-c copy_all_comments`
- Tags passed via `-c KEY=VALUE` (one `-c` argument per tag) — the real oggenc comment syntax
- Cover art: not supported by oggenc CLI; skipped silently

### `opuswrapper.cpp`
- Removed fake flag `--copy-comments`
- Tags passed via `--comment KEY=VALUE` (one `--comment` per tag)
- Cover art passed via `--picture <path>`

---

## Behaviour

| | FLAC | MP3 | Ogg Vorbis | Opus |
|---|---|---|---|---|
| Text tags | ✅ | ✅ | ✅ | ✅ |
| Cover art | ✅ | ✅ | ❌ (oggenc limitation) | ✅ |

- If `ffprobe` is not found: tags are not transferred, conversion still proceeds (warning logged)
- If `ffmpeg` is not found: cover art is not transferred, conversion still proceeds (warning logged)
- If the source file has no cover art: `ffmpeg` returns non-zero, `extractCoverArt` returns empty string, conversion proceeds normally

---

## Verified
Tested with WAV → FLAC conversion. Output confirmed to contain all 16 source tags and a 600×606 JPEG cover art block (`type: 3, Cover (front)`) via `metaflac --list`.
