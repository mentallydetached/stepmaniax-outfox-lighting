# StepManiaX Stage Lighting Engine for OutFox

This is a custom hardware lighting driver for Project OutFox that connects directly to StepManiaX (SMX) dance pads. 

By default, OutFox routes lighting through its own internal engine, which can sometimes compress the lighting data or turn off the lights entirely while you are in the menus. I wanted my stage to react directly to my footsteps and stay lit up outside of songs, so I wrote this plugin. It disguises itself as a PacDrive controller so OutFox will load it, but it actually talks directly to the SMX pad sensors.

If you have an SMX stage and play OutFox, feel free to use it. 

### Features
* **Direct Sensor Polling:** The lights react instantly to your footsteps because the plugin polls the physical hardware sensors directly, rather than waiting for the game engine to tell the pad to light up.
* **Dual-Intensity Lighting:** Game beats pulse the background shapes smoothly, but physically stepping on the arrows creates a brighter flash so you can see your own steps.
* **Auto-Shuffling Themes:** When a new song starts, the plugin randomly shuffles between different LED shapes (Chevrons, Diamonds, Blocks) and color themes (Synced Rainbow, Offset Rainbow, Static).
* **Idle Animations:** The pad breathes softly when you are in the song wheel or menus.

### Installation

*Note: This was built for the 64-bit version of Project OutFox.*

1. Download the latest `SMX-OutFox-Lighting.zip` from the **Releases** tab.
2. Extract the two files (`pacdrive64.dll` and `SMX.dll`).
3. Place both files directly into your OutFox `Program` folder (the same folder where `OutFox.exe` lives).
   * *Example:* `C:\OutFox\Program\`
4. Open your OutFox `Preferences.ini` file (usually located in the `Save` folder) and change your lights driver:
   ```ini
   LightsDriver=PacDrive
   ```
5. Boot the game. The pad should wake up and start the idle animation.

### Compiling from Source
If you prefer to build the DLL yourself from the source code, open an x64 Native Tools Command Prompt for VS and run:
```
cl /LD /MT /EHsc smx_stage_pacdrive.cpp SMX.lib /Fe:pacdrive64.dll
```
