# Disco Sauce 1 - all-in-one vocal plugin (VST3)  |  flowmotion

Compressor, EQ (with de-esser), delay, reverb and built-in key/scale auto-tune, with four main presets.
Fully offline: no internet, accounts or licence checks, ever.

## Get the finished plugin (no coding, about 10 minutes)

1. Make a free account at github.com and click **New repository** (name it `disco-sauce`, set it to Private, create it).
2. On the new repository page click **uploading an existing file**, then drag in EVERYTHING from inside this folder:
   `CMakeLists.txt`, `README.md`, the `Source` folder, the `Assets` folder and the `.github` folder.
   (If your computer hides the `.github` folder, see "If .github won't upload" below.)
   Click **Commit changes**.
3. Click the **Actions** tab. A build called "Build Disco Sauce" starts by itself (about 8-12 minutes).
4. When it shows a green tick, open it and scroll to **Artifacts** at the bottom. Download
   `DiscoSauce-Windows-VST3` (Windows) or `DiscoSauce-macOS-VST3` (Mac).
5. Unzip it.

If the build shows a red cross: open it, copy the red error text and send it to me. I'll fix it.

## Install in FL Studio

**Windows:** copy the folder `Disco Sauce 1.vst3` into `C:\Program Files\Common Files\VST3`.
**Mac:** copy `Disco Sauce 1.vst3` into `/Library/Audio/Plug-Ins/VST3`, then run in Terminal:
`xattr -cr "/Library/Audio/Plug-Ins/VST3/Disco Sauce 1.vst3"`

In FL Studio: Options > Manage plugins > **Find installed plugins** (Verify plugins), then add
"Disco Sauce 1" from the plugin list on a mixer insert, on the vocal track. Needs FL Studio 20.8 or newer (VST3).

## How to use

* Click a preset: CRISP, T-PAIN, DARK TRAP or DISCO PRO. It sets all five knobs and the retune speed.
* Every knob is the AMOUNT of that section (turn to the left to switch the section off).
* DRY/WET slider = how much of the whole effect you hear.
* KEY + SCALE: set these to your song's key so the auto-tune only sings notes that fit.
  Scales: Chromatic, Major, Minor, Harmonic Minor, Pentatonic Major/Minor, Blues, Dorian, Phrygian,
  Mixolydian, Lydian, Hijaz (Arabic).
* RETUNE SPEED: 0 ms = instant robotic hard-tune (T-Pain), higher = more natural.
* OUTPUT: final volume trim. Double-click any knob to reset it.
* Delay is synced to the BPM of your FL Studio project.
* The plugin reports its latency (about 20 ms) so FL Studio keeps everything in time.

## If .github won't upload

On your repository page: **Add file > Create new file**, type the name `.github/workflows/build.yml`
(typing the slashes creates the folders), paste the contents of the build.yml file from this project, commit.

## Project layout

* `Source/dsp/AutoTune.h` pitch detection (YIN) + pitch-synchronous shifter
* `Source/dsp/Dsp.h` filters, compressor, ping-pong delay, 8-line FDN reverb
* `Source/dsp/Presets.h` the four presets and the scale tables
* `Source/PluginProcessor.*` signal chain and parameters, `Source/PluginEditor.*` interface
* `Assets/design.jpg` the artwork
