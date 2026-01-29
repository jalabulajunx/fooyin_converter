**NOTE:** This project was completely written by AI. It doesn't prove anything except for the fact that it helps me in finding alternative for my workflow. If this helps, go ahead and use it as you like.

# Fooyin Audio Converter Plugin

A lightweight audio conversion plugin for [Fooyin music player](https://github.com/ludouzi/fooyin) that uses dedicated codec executables for high-quality audio format conversion.

<img width="1277" height="764" alt="image" src="https://github.com/user-attachments/assets/59f9a9d2-e60b-47ae-bf47-929643f246dc" />

## Features

- **Multiple Format Support**: Convert between MP3, FLAC, Opus, and Ogg Vorbis
- **Batch Conversion**: Convert multiple tracks at once from the playlist
- **Context Menu Integration**: Right-click tracks to convert directly
- **Quality Control**: Adjustable bitrate/quality settings per format
- **Progress Tracking**: Real-time conversion progress display
- **Metadata Preservation**: Keeps your audio tags intact
- **Configurable**: Customize window size and default codec
- **Simple Interface**: Clean, intuitive UI integrated into Fooyin

## Supported Formats

### Output Formats
- **MP3** (via LAME encoder)
  - CBR: 96-320 kbps
  - VBR: V0, V2 quality presets
- **FLAC** (lossless)
  - Compression levels 0-8
- **Opus**
  - Bitrates: 64-256 kbps
- **Ogg Vorbis**
  - Quality levels 2-10

### Input Formats
Any format supported by the codec tools (WAV, FLAC, MP3, Ogg, Opus, M4A, AAC, WavPack, APE, etc.)

## Dependencies

### Required
- Qt 6.2+
- Fooyin 0.8+
- CMake 3.18+

### Codec Tools (at least one required)
Install the audio codec executables you need:

```bash
# Debian/Ubuntu
sudo apt install flac lame opus-tools vorbis-tools

# Fedora
sudo dnf install flac lame opus-tools vorbis-tools

# Arch Linux
sudo pacman -S flac lame opus-tools vorbis-tools

# macOS (via Homebrew)
brew install flac lame opus-tools vorbis-tools
```

**Note**: The plugin will detect which codecs are installed and only show available formats.

## Building

### 1. Install Fooyin Development Files

First, ensure you have Fooyin installed with development files:

```bash
# If building Fooyin from source
cd /path/to/fooyin
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build .
sudo cmake --install .
```

### 2. Build the Plugin

```bash
cd fooyin-converter
mkdir build && cd build

# Configure (adjust paths as needed)
cmake .. \
    -DCMAKE_PREFIX_PATH=/usr/local \
    -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build .

# Install
sudo cmake --install .
```

### Custom Install Location

To install to your user directory instead:

```bash
cmake .. \
    -DCMAKE_PREFIX_PATH=/usr/local \
    -DCMAKE_INSTALL_PREFIX=~/.local

cmake --build .
cmake --install .
```

## Usage

### Quick Start: Convert from Playlist (Recommended)

1. **Select tracks** in your Fooyin playlist
   - Single track: Click on any track
   - Multiple tracks: Shift+Click or Ctrl+Click to select multiple
2. **Right-click** → **Convert Audio...**
3. **Choose format and quality**
4. **Click Convert**
5. Watch the progress!

**Single Track Mode:**
- Shows the full file path
- Output path auto-generated in same directory

**Batch Mode (Multiple Tracks):**
- Shows "X files selected"
- All files output to same directory
- Sequential processing with progress updates

### Alternative: Widget Mode

You can also add the converter as a persistent widget:

1. Launch Fooyin
2. Enter **Layout Editing Mode** (View → Layout Editing)
3. Right-click on your layout
4. Select **Add Widget** → **Audio Converter**
5. Position it where you want
6. Exit Layout Editing Mode

Then use the widget to convert files by browsing manually.

### Configuration

Customize the converter in **Settings → Plugins → Audio Converter**:
- **Window Size**: Set dialog width/height (applies on next open)
- **Default Codec**: Pre-select your preferred format (FLAC, MP3, Opus, Ogg)

## Configuration Tips

### Best Quality Settings

- **Lossless**: FLAC with compression level 8
- **Near-transparent lossy**: MP3 V0 or Opus 192 kbps
- **Good quality**: MP3 320 kbps or Opus 128 kbps
- **Podcast/Voice**: Opus 64 kbps mono

### Common Use Cases

**CD Rip to Modern Format**:
- Input: WAV or FLAC
- Output: Opus 128 kbps
- Result: ~1 MB per minute, excellent quality

**Archive to Portable**:
- Input: FLAC (lossless)
- Output: MP3 320 kbps or V0
- Result: Compatible with all devices

**Reduce File Size**:
- Input: FLAC or high-bitrate MP3
- Output: Opus 96 kbps
- Result: 50-75% size reduction, minimal quality loss

## Troubleshooting

### Plugin Not Loading

Check Fooyin logs:
```bash
cat ~/.config/fooyin/logs/fooyin.log
```

Look for Audio Converter plugin messages.

### "No codecs available" Error

Install the required codec tools:
```bash
# Verify installation
which flac lame opusenc oggenc

# Check versions
flac --version
lame --version
opusenc --version
oggenc --version
```

### Conversion Fails

Common causes:
1. **Input file corrupt**: Try a different file
2. **Output directory doesn't exist**: Create it first
3. **Insufficient disk space**: Free up space
4. **Unsupported input format**: Convert to WAV first

Enable verbose logging in Fooyin settings to see detailed error messages.

## Architecture

This plugin uses a modular architecture with dedicated codec executables:

### Components

- **CodecWrapper**: Abstract base class for codec implementations
- **FlacWrapper**: FLAC encoding/decoding via `flac` command
- **LameWrapper**: MP3 encoding via `lame` command
- **OpusWrapper**: Opus encoding via `opusenc` command
- **OggWrapper**: Ogg Vorbis encoding via `oggenc` command
- **ConversionManager**: Coordinates conversions, codec detection, and process management
- **ConverterWidget**: Qt-based UI with batch support
- **ConverterPlugin**: Integrates with Fooyin (CorePlugin + GuiPlugin)

### Design Philosophy

Unlike FFmpeg-based solutions, this uses **dedicated codec executables** (recommended by Naren):

**Advantages:**
- **Better Quality**: Specialized encoders like LAME produce superior MP3 quality
- **Simpler Debugging**: Individual tools are easier to troubleshoot
- **Predictable Behavior**: Each tool does one thing well
- **Direct Control**: Access to all codec-specific options
- **Unix Philosophy**: Composable, single-purpose tools

**How It Works:**
1. Plugin detects installed codecs on startup (`which flac`, etc.)
2. User selects format → Manager spawns appropriate codec process
3. QProcess monitors stderr for progress updates
4. Regex parsers extract progress percentages
5. Signals update UI in real-time

## Roadmap

### ✅ Completed
- [x] Batch conversion (multiple files at once)
- [x] Integration with Fooyin track selection (right-click → Convert)
- [x] Progress tracking and reset
- [x] Settings page with configuration options

### 🔮 Future Enhancements
- [ ] Per-track progress table in batch mode
- [ ] Overall progress bar for batches
- [ ] Preset system (save favorite settings)
- [ ] Parallel conversion (use multiple cores)
- [ ] Pause/Resume batch operations
- [ ] ReplayGain calculation
- [ ] Cue sheet splitting
- [ ] MusicBrainz metadata integration
- [ ] Custom output filename patterns

## Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Follow existing code style
4. Test your changes
5. Submit a pull request

## License

GPL-3.0 License - See LICENSE file for details

## Credits

- Inspired by foobar2000's foo_converter component
- Built for the [Fooyin music player](https://github.com/ludouzi/fooyin) project
- Architecture guidance and codec recommendation by **Naren** (use dedicated binaries instead of FFmpeg)
- Uses excellent open-source codec tools: FLAC, LAME, Opus, Vorbis

## Support

- Report issues: [GitHub Issues](https://github.com/fooyin/fooyin-converter/issues)
- Fooyin Discord: [Join here](https://discord.gg/fooyin)
- Documentation: [Wiki](https://github.com/fooyin/fooyin-converter/wiki)
