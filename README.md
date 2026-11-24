# Audio Plugin Suite

A modular C++20 / JUCE 8 audio plugin suite (**VST3**, **CLAP**, and **Standalone** for Windows x64) featuring zero-allocation real-time DSP and lock-free shared-memory **inter-plugin linking** (`SuiteHarmonyLink` & `SuiteTextureLink`) so plugins talk directly across tracks, formats (VST3 ↔ CLAP ↔ Standalone), and DAWs with zero sidechain wiring.

---

## Included Plugins (`v0.1.0`)

### 1. SessionMate — *Reactive 6-Player Virtual Band, Style DNA Engine & Multi-VST3 Host*
- **6 Independent Virtual Band Members:** Drums, Bass (with **Kick-Lock %**), Keys (**5 voicing styles**: Close, Drop-2, Rootless Jazz, Shell 3-7, Open Spread), Rhythm Guitar (biomechanical fretboard strum), Percussion, and Horns/Strings (**Call-and-Response** fills in gaps).
- **32 Styles Across 6 Genre Families + Live Style A ↔ B DNA Morphing** + odd meters (`3/4, 4/4, 5/4, 6/8, 7/8, 12/8`), Song Arranger (`Intro → Outro`, `+Fill`, `1/2x`/`2x`), 7 quick-start chord progressions, and per-slot 2/4-bar **Pattern Lock**.
- **Built-In Multi-Instrument Synth + SFZ/WAV Trigger + Studio Room Reverb**, PLUS per-slot **Third-Party VST3 Sub-Hosting** (crash-safe scanner, native GUI popup, 7 Drum-Map presets: GM, Toontrack SD3/EZD, AD2, SSD5, GGD, BFD3, Custom, and graceful Built-In fallback for collaborators).
- **3 Sync Modes** (DAW Host Sync, Live Audio Follow, Tap/Standalone), multi-out audio (Master + 6 stereo buses), multi-channel MIDI out (`Ch 1–6`), and **32-bar drag-to-DAW `.mid` export**.

### 2. HarmonyHUD — *Polyphonic Audio-to-Music-Theory X-Ray, Band Voicing Map & Reharmonizer*
- **Zero-Coloration Pass-Through** with dual-rate 216-bin CQT + 16-iter NMF harmonic salience solver + 301-state music-theory HMM (25 chord qualities + slash-bass chords) + Krumhansl-Schmuckler key & 13-mode estimator.
- **Interactive C2–C6 Keyboard & 6-String Guitar Fretboard** (`Standard / Drop-D / DADGAD`) with Scale, 3rd/7th Guide-Tone, Pentatonic, Active Mode, Tension overlays, and CAGED fret-position filters.
- **5-Chip Categorized Reharmonization Palette** (*Functional, Tritone Sub, Sec. Dom, Modal Borrow, Color/Slash*) with local Rhodes audition, interactive progression editor, and drag-to-DAW `.mid` export.
- **Audio Sidechain & Zero-Sidechain Band Clash Radar** highlighting live minor-2nd and tritone rubs.

### 3. Fossil — *3-Way Transient / Tonal / Noise Freeze & 4-Strata Granular Engine*
- **Phase-Accurate STFT HPSS Decomposition** (exact `1024`-sample PDC) separating live input into **Transient (T)**, **Tonal (H)**, and **Air/Noise (N)** streams with independent Dry Transient Pass.
- **4 Stackable Geological Strata (`L1–L4`)**: Zero-click capture (`Freeze T / H / N` or MIDI notes `48/60, 50/62, 52/64`), 4-head cubic Hermite granular pitch-shifting (`-24..+24 st`), drift, tilt, M/S width, vertical drag-to-reorder, Crossfade Focus rail, 4-channel Householder FDN Shimmer Diffusion, and a tempo-synced **Bjorklund Euclidean Gate** (`1–16` steps).

### 4. VowelMorph — *5-Band ZDF Vocal-Tract Formant Resonator & Syllable Sequencer*
- **5-Band Parallel Zero-Delay Feedback (ZDF) SVF Formant Bank** across 10 IPA phonemes (`EE..MM`), 8 vocal-tract profiles (`Bass, Tenor, Alto Choir, Soprano, Child, Whisper Air, Talk-Box Synth, Alien/Giant`), draggable 2D IPA Vowel Triangle with live log-frequency formant curve, Throat Size, Jaw Open, Stereo Choir Width, `F3–F5` Breath Air aspiration, and Consonant Closure dips.
- **4 Modulation Modes:** Envelope Follower, Tempo-Synced LFO, **8-Step Syllable Sequencer** (rests, accents, glide), and MIDI vowel trigger + 18 factory presets.

---

## Inter-Plugin Linking (Zero Sidechain Wiring)

All 4 plugins share lock-free, zero-allocation Windows Named Shared Memory buses (`Bus A–H`, 1500 ms peer heartbeat):

- **`SuiteHarmonyLink` (`SessionMate` ↔ `HarmonyHUD` → `Fossil` & `VowelMorph`):**
  - `HarmonyHUD` feeds live polyphonic chords, modes, quantized reharmonizations (`Now / Beat / Bar`), full chord charts (`[Send → Band]`), and clash-avoidance masks directly into `SessionMate`.
  - `SessionMate` broadcasts active chord, **Next-Chord Lookahead**, section/style, and live per-slot MIDI bitmasks back to `HarmonyHUD`'s **Band X-Ray** view (`Input | Band | Both + Clash`).
  - `Fossil` (`[Harmonic Quantize]`) and `VowelMorph` (`Chord Choir` + `Section Lift`) can also listen read-only to `SuiteHarmonyLink` to stay harmonically locked to your live band.
- **`SuiteTextureLink` (`Fossil` ↔ `VowelMorph`):**
  - `Fossil` streams live `T / H / N` energies, Freeze capture triggers (sweeping VowelMorph `OO → AH → EE`), and Euclidean Gate hits (advancing VowelMorph's Syllable Sequencer 1-to-1 in lockstep).
  - `VowelMorph` streams live `F1–F5` formants, `(X, Y)` coordinates, and brightness back to sweep Fossil's Crossfade Focus rail and color its Shimmer Diffusion tail.

---

## Installation (Pre-Built Windows x64 Releases)

Download the `.zip` archive from the [GitHub Releases](../../releases) page:
- **VST3:** Copy `*.vst3` folders to `C:\Program Files\Common Files\VST3\` (or your DAW's custom VST3 folder).
- **CLAP:** Copy `*.clap` files to `C:\Program Files\Common Files\CLAP\` (or your DAW's custom CLAP folder).
- **Standalone:** Run `SessionMate.exe`, `HarmonyHUD.exe`, `Fossil.exe`, or `VowelMorph.exe` directly.

---

## Building From Source (Windows x64)

**Requirements:** Visual Studio 2022 (or Build Tools with MSVC + CMake + Ninja) and Git.

```powershell
# 1. Fetch JUCE 8.0.6 and clap-juce-extensions into third_party/
powershell -ExecutionPolicy Bypass -File scripts\setup_deps.ps1

# 2. Build Release VST3 + CLAP + Standalone and run unit tests for any plugin
powershell -ExecutionPolicy Bypass -File scripts\build_plugin.ps1 -Plugin SessionMate -RunTests
powershell -ExecutionPolicy Bypass -File scripts\build_plugin.ps1 -Plugin HarmonyHUD -RunTests
powershell -ExecutionPolicy Bypass -File scripts\build_plugin.ps1 -Plugin Fossil -RunTests
powershell -ExecutionPolicy Bypass -File scripts\build_plugin.ps1 -Plugin VowelMorph -RunTests
```

Compiled binaries are staged into `dist\<PluginName>\`.

---

## Verification Status

All 4 released plugins pass automated DSP, real-time safety, and live hosted VST3-to-VST3 integration suites (**223 / 223 checks passed, 0 failed**):
- `SessionMateTests`: **54 / 54 PASS**
- `HarmonyHUDTests`: **37 / 37 PASS**
- `JointHarmonyLinkTest` (live hosted `HarmonyHUD.vst3` ↔ `SessionMate.vst3`): **29 / 29 PASS**
- `FossilTests`: **39 / 39 PASS**
- `VowelMorphTests`: **20 / 20 PASS**
- `JointTextureLinkTest` (live hosted `Fossil.vst3` ↔ `VowelMorph.vst3` + `SuiteHarmonyLink`): **44 / 44 PASS**
