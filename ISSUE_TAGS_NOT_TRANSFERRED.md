# Issue: Audio metadata tags are not transferred during conversion

## Summary
When converting audio files between formats, all metadata tags (artist, album, title, track number, genre, year, cover art, etc.) were lost and not copied to the output file.

## Root Cause

The issue had two distinct layers:

### 1. Fabricated encoder flags
The previous fix attempted to use encoder CLI flags that do not exist:

| Codec | Fake Flag Used | Result |
|-------|---------------|--------|
| FLAC | `--import-tags-from <wav>` | Exit code 1, no output — flag only accepts FLAC/Ogg files, not WAV |
| FLAC | `--keep-foreign-metadata-if-fail` | Exit code 1, no output — flag does not exist in flac 1.5.0 |
| MP3 (LAME) | `--tagcopy all` | Not a real LAME flag |
| Ogg Vorbis | `-c copy_all_comments` | Not a real oggenc flag |
| Opus | `--copy-comments` | Not a real opusenc flag |

The FLAC encoder was failing silently (exit code 1, empty stderr/stdout) because stderr was being consumed by the progress-monitoring signal handler before the error handler could read it, masking the actual error message.

### 2. No cross-format tag transfer mechanism
None of these CLI encoders have a universal "copy all tags from input" flag that works across format boundaries (e.g. WAV → FLAC). Each encoder reads audio data from the input but does not automatically convert the source file's metadata format into its own tag format.

## Impact
All converted files contained no metadata, making them unrecognisable in music library applications. FLAC conversion was additionally broken entirely (silent failure) due to the invalid flags.

## Resolution
See `README_METADATA_FIX.md` for full details of the fix applied.
