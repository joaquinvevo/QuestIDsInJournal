# Quest IDs in Journal

A small SKSE utility for **Skyrim Special Edition** that displays quest information directly in the journal.

I originally wanted something simple: a way to see a quest's **EditorID**, **FormID**, and **current stage** without having to search for them outside the game. The project grew a little along the way, so it now includes a stage inspector, clipboard shortcuts, and an MCM.

**Tested game version: Skyrim SE 1.5.97.** Support for other runtimes has not been verified.

<!-- Optional: add a real in-game screenshot here when one is available. -->

## Features

- Display the selected quest's EditorID, FormID, and current stage in the journal.
- Show or hide individual fields and adjust the label's position and font size.
- Browse a quest's defined stages in the **Stage Inspector** and check which ones have been completed.
- Navigate between stage pages and open a basic details view.
- Copy EditorIDs, FormIDs, stage numbers, or a ready-to-paste `setstage` console command.
- Configure display settings and hotkeys through **SkyUI MCM**.
- Optionally request and confirm stage execution for debugging (**experimental; disabled by default**).

The plugin draws its information at runtime rather than replacing the journal's SWF file.

## Requirements

- **Skyrim Special Edition 1.5.97** (the only version tested so far)
- **SKSE64 2.0.20**
- **Address Library for SKSE Plugins** (Special Edition / 1.5.97 version)
- **SkyUI** (for the Mod Configuration Menu)

You do **not** need the Apostasy modlist. It was simply the environment used during development and testing.

## Installation

1. Install the requirements above.
2. Install the compiled mod archive with **Mod Organizer 2** or **Vortex**.
3. Enable `QuestIDsInJournal_MCM.esp` in your load order.
4. Launch Skyrim through **SKSE**.
5. Open the quest journal, or go to **Mod Configuration > Quest IDs in Journal** to adjust the settings.

The ESP is ESL-flagged. The compiled mod archive should include the DLL, INI, ESP, SEQ, and Papyrus PEX files. **This repository contains the development source**, not a ready-to-install replacement for the compiled mod archive.

## Default Hotkeys

All shortcuts use **Ctrl + Shift + the key shown below**. You can remap the final key in the MCM (A–Z or 0–9); Ctrl + Shift stays fixed.

| Action | Key |
| --- | :---: |
| Copy EditorID | C |
| Copy FormID | F |
| Toggle Stage Inspector | I |
| Next / previous inspector page | N / P |
| Select next / previous stage | J / K |
| Toggle list / details | D |
| Copy selected stage number | S |
| Copy `setstage` command | G |
| Request stage execution (experimental) | V |
| Confirm stage execution (experimental) | R |

These shortcuts are intended for use while the quest journal is open.

## Experimental Stage Execution

**This feature is disabled by default.** You can enable it under **MCM > Advanced**.

It requires two separate actions: request a stage with **Ctrl + Shift + V**, then confirm with **Ctrl + Shift + R** within eight seconds. The keys can be reassigned in the MCM.

**Warning:** Executing stages can break quest progression or scripts, even when the request is accepted by Papyrus. Use a disposable test save or make a backup before trying it. Do not repeatedly submit requests while a previous execution is still processing. This feature is for experimentation and troubleshooting, **not recommended for regular gameplay**.

You can use everything else in the mod without enabling stage execution.

## Compatibility and Limitations

- **Confirmed working:** Skyrim Special Edition **1.5.97**.
- **Not yet tested:** Anniversary Edition runtimes, Skyrim VR, and other game versions.
- Major journal UI replacements may need patches. Compatibility with every UI overhaul is not guaranteed.
- The stage inspector currently skips quests with more than **96 defined stages** instead of showing a partial result.
- Stage details show basic information; they do not currently display the quest's full log-entry text.

## About the Project and AI Disclosure

I made this because it was a small feature I genuinely wanted in my own Skyrim playthrough. I don't have much experience developing SKSE plugins, so I used generative AI to create the code and Papyrus scripts while I handled the original idea, feature decisions, testing in-game, and iteration.

**The mod's code was created through AI-assisted development.** I'm being upfront about that. I tested it and kept adjusting it until it worked the way I wanted.

This is a personal hobby project. I hope someone else finds it useful too.

## Building from Source

The SKSE plugin is written in C++ and uses **CommonLibSSE-NG**, **CMake**, **vcpkg**, and **Visual Studio 2022 / MSVC build tools** on Windows. You will also need Git and Ninja.

To keep this repository small, the third-party dependencies in `lib/` are **not included**. Download them separately into the paths expected by `CMakePresets.json` and `CMakeLists.txt`.

From the root of the cloned QuestIDsInJournal repository, run in PowerShell:

```powershell
New-Item -ItemType Directory -Force lib | Out-Null
git clone --branch ng https://github.com/alandtse/CommonLibSSE-NG.git lib/commonlibsse-ng
git clone https://github.com/microsoft/vcpkg.git lib/vcpkg
.\lib\vcpkg\bootstrap-vcpkg.bat

cmake --preset release-windows
cmake --build --preset release-windows
```

If you already have either dependency folder, keep it rather than cloning over it. These commands fetch upstream versions; the exact revisions used to compile the published DLL are not pinned here, so newer upstream changes may require adjustments.

The SkyUI MCM uses two Papyrus source files, `QIJ_MCM.psc` and `QIJ_MCMBridge.psc`, which must be compiled separately to `.pex` files for the installable version. The MCM ESP and SEQ must also be included in the distributed mod archive.

## Contributions and License

Bug reports, compatibility patches, forks, modifications, and improvements are welcome. You do not need to ask me before making a fork or creating your own version, provided you follow the applicable licenses.

This repository uses the **GNU General Public License version 3 or later (GPL-3.0-or-later)**; see [LICENSE](LICENSE). Under this license you may study, modify, and redistribute the code, but redistribution comes with requirements, including preserving applicable notices and providing corresponding source when required. I don't ask for additional personal credit beyond the notices the license requires.

The project started from the [CommonLibSSE-NG template](https://github.com/codepuncher/CommonLibSSE-NG-template) and uses third-party dependencies that retain their own licensing terms. The project license does not grant any rights to Bethesda game assets or other third-party material.

## Credits

- Bethesda Game Studios — Skyrim Special Edition
- SKSE team — Skyrim Script Extender
- SkyUI team — Mod Configuration Menu
- CommonLibSSE NG contributors — SKSE plugin development library
- Generative AI tools — Code and scripting assistance
