# Plugin Factory

Autonomous audio-plugin factory built with JUCE 8 + CMake.
See `CLAUDE.md` for the conventions every change must follow.

## Plugin catalog

<!-- BEGIN:CATALOG -->

### Shipped (0)

_None yet._


### In progress (8)

| Plugin | Category | Reference |
| --- | --- | --- |
| Bus Compressor | Dynamics | SSL G-series bus comp |
| Dynamic Parametric EQ | EQ | FabFilter Pro-Q 4 |
| Granular Delay | Delay | Granular cloud delay (pitch + tempo-sync) |
| Resonance Suppressor | EQ | oeksound soothe2 |
| Saturator | Saturation | Analog tape / tube soft-clip |
| Shimmer Reverb | Reverb | FDN shimmer (octave-up feedback) |
| Single-Band EQ | EQ | RBJ Audio EQ Cookbook — peaking EQ |
| Vocal MB Comp | Dynamics | Vocal-tuned 3-band compressor |


### Planned (0)

_None yet._

<!-- END:CATALOG -->

## Install

Each GitHub Release is a single consolidated build of the whole factory, tagged
`<year>.<n>` (e.g. `2026.1`). Grab either the everything bundle for your OS
(`tatsunari-plugins-<tag>-macOS.zip` / `-Windows.zip`) or an individual
plugin zip (`<plugin>-<version>-macOS.zip` / `-Windows.zip`), then copy the
bundles into your plugin folders:

- **AU** (`.component`) → `~/Library/Audio/Plug-Ins/Components/` (or `/Library/...` for all users)
- **VST3** (`.vst3`) → `~/Library/Audio/Plug-Ins/VST3/` (or `/Library/...`)

### curl で最新版をインストール

The snippets below install the **everything bundle** (all plugins). Asset
filenames embed the release version, so instead of hard-coding a URL each snippet
asks the GitHub API for the **latest** release and downloads from there (only
`curl` and `unzip` — no `jq`). Downloading with `curl` also skips the
`com.apple.quarantine` flag a browser attaches, so the macOS "damaged" prompt
below does **not** appear. Restart your DAW and rescan afterwards.

#### macOS — AU (`.component`)

    REPO=6sRyuSK/tatsunari-plugins
    url=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
          | grep -oE '"browser_download_url": *"[^"]*macOS-AU\.zip"' | cut -d'"' -f4)
    dest="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$dest"
    curl -fL "$url" -o /tmp/tp-au.zip && unzip -o /tmp/tp-au.zip -d "$dest" && rm -f /tmp/tp-au.zip
    killall -9 AudioComponentRegistrar 2>/dev/null   # force an AU rescan

#### macOS — VST3 (`.vst3`)

    REPO=6sRyuSK/tatsunari-plugins
    url=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
          | grep -oE '"browser_download_url": *"[^"]*macOS-VST3\.zip"' | cut -d'"' -f4)
    dest="$HOME/Library/Audio/Plug-Ins/VST3"
    mkdir -p "$dest"
    curl -fL "$url" -o /tmp/tp-vst3.zip && unzip -o /tmp/tp-vst3.zip -d "$dest" && rm -f /tmp/tp-vst3.zip

#### Windows — VST3 (`.vst3`, PowerShell)

    $repo = "6sRyuSK/tatsunari-plugins"
    $url  = (Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest").assets |
            Where-Object { $_.name -like "*-Windows.zip" } |
            Select-Object -ExpandProperty browser_download_url
    $dest = "$Env:CommonProgramFiles\VST3"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Invoke-WebRequest $url -OutFile "$Env:TEMP\tp-win.zip"
    Expand-Archive "$Env:TEMP\tp-win.zip" -DestinationPath $dest -Force
    Remove-Item "$Env:TEMP\tp-win.zip"

### macOS: "「…」は壊れているため開けません" / "…is damaged and can't be opened"

The release binaries are **not code-signed or notarized**, and macOS attaches a
quarantine flag (`com.apple.quarantine`) to anything downloaded via a browser.
Gatekeeper then reports the unsigned bundle as *damaged* — the plugin is fine,
it is just quarantined. Strip the flag after installing:

    # Adjust the names/paths to what you installed (use ~/Library if you installed per-user).
    sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Resonance Suppressor.component"
    sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Resonance Suppressor.vst3"
    sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Dynamic Parametric EQ.component"
    sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Dynamic Parametric EQ.vst3"

Then restart your DAW and rescan (for AU you may also need
`killall -9 AudioComponentRegistrar`).

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
