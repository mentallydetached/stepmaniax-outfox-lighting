#include <windows.h>
#include <cstdint>
#include <fstream>
#include <string>
#include <mutex>
#include <bitset>
#include <vector>
#include <cmath>
#include <thread>
#include <atomic>
#include <ctime>
#include "SMX.h"

const int BUFFER_SIZE = 1350;
uint8_t lightBuffer[BUFFER_SIZE] = {0};
bool isSmxInitialized = false;

// Logger setup
std::mutex logMutex;
std::string logFilePath = "smx_plugin_debug.log";
unsigned short lastData = 0xFFFF;

// SET TO TRUE TO ENABLE LOGGING, FALSE TO RUN SILENTLY
bool ENABLE_DEBUG_LOGGING = false; 

void WriteLog(const std::string& message) {
    if (!ENABLE_DEBUG_LOGGING) return; // Instantly exit if logging is disabled
    
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
}

// SMX Callback
void SMX_Update_Callback(int pad, int reason, void* user) {
    if (reason == 0) {
        WriteLog("[SMX] Pad " + std::to_string(pad) + " connected successfully.");
    }
}

// Graphics & Thread Engine State
std::atomic<bool> isRunning{false};
std::thread renderThread;
std::atomic<unsigned short> latestOutfoxData{0};

// NEW: Dual intensity tracking for differentiation
float gameIntensity[2][9] = {{0}}; 
float physIntensity[2][9] = {{0}}; 
float globalHue = 0.0f;

// --- MODULAR THEME ENGINE ---
int colorTheme = 0;
int shapeTheme = 0;
int effectTheme = 0;

// 5x5 Pixel Art Shape Definitions (1 = LED On, 0 = LED Off)
// Correctly mapped to SMX Hardware: Outer 4x4 Grid (0-15) + Inner 3x3 Grid (16-24)
const uint8_t SHP_CHEV_UP[25] = {
    0,1,1,0, 1,0,0,1, 0,0,0,0, 0,0,0,0,  // Outer
     1,0,1,   0,0,0,   0,0,0             // Inner
};
const uint8_t SHP_CHEV_DN[25] = {
    0,0,0,0, 0,0,0,0, 1,0,0,1, 0,1,1,0,
     0,0,0,   0,0,0,   1,0,1
};
const uint8_t SHP_CHEV_LF[25] = {
    0,1,0,0, 1,0,0,0, 1,0,0,0, 0,1,0,0,
     1,0,0,   0,0,0,   1,0,0
};
const uint8_t SHP_CHEV_RT[25] = {
    0,0,1,0, 0,0,0,1, 0,0,0,1, 0,0,1,0,
     0,0,1,   0,0,0,   0,0,1
};
const uint8_t SHP_DIAMOND[25] = {
    0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0,
     0,1,0,   1,0,1,   0,1,0
};
const uint8_t SHP_FULL[25] = {
    1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
     1,1,1,   1,1,1,   1,1,1
};

// HSV to RGB Math Helper
void HSVtoRGB(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    int i = static_cast<int>(std::fmod(h / 60.0f, 6.0f));
    float f = (h / 60.0f) - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    float rf = 0, gf = 0, bf = 0;
    switch (i) {
        case 0: rf = v; gf = t; bf = p; break;
        case 1: rf = q; gf = v; bf = p; break;
        case 2: rf = p; gf = v; bf = t; break;
        case 3: rf = p; gf = q; bf = v; break;
        case 4: rf = t; gf = p; bf = v; break;
        case 5: rf = v; gf = p; bf = q; break;
    }
    r = static_cast<uint8_t>(rf * 255);
    g = static_cast<uint8_t>(gf * 255);
    b = static_cast<uint8_t>(bf * 255);
}

// Panel Configuration for Colors and Bit Mapping
struct PanelConfig {
    int index;
    float hueOffset;     // Base hue offset for the rainbow cycle
    float intensity;     // Current brightness (0.0 to 1.0)
    int bit1, bit2;      // The bits in the PacDrive payload to listen to
};

// StepMania/OutFox PacDrive Standard Mapping:
// Map all 9 panels. The corners mirror the bass bits to keep the whole pad alive.
std::vector<PanelConfig> panels = {
    {0, 0.0f,   0.0f, 14, 15}, // Top-Left
    {1, 40.0f,  0.0f, 6,  10}, // Up
    {2, 80.0f,  0.0f, 14, 15}, // Top-Right
    {3, 120.0f, 0.0f, 4,  8},  // Left
    {4, 160.0f, 0.0f, 14, 15}, // Center
    {5, 200.0f, 0.0f, 5,  9},  // Right
    {6, 240.0f, 0.0f, 14, 15}, // Bottom-Left
    {7, 280.0f, 0.0f, 7,  11}, // Down
    {8, 320.0f, 0.0f, 14, 15}  // Bottom-Right
};

// Resolves which shape to draw based on the current randomized theme
const uint8_t* GetPanelShape(int panelIndex, int currentShapeTheme) {
    if (currentShapeTheme == 0) {
        if (panelIndex == 1) return SHP_CHEV_UP;
        if (panelIndex == 7) return SHP_CHEV_DN;
        if (panelIndex == 3) return SHP_CHEV_LF;
        if (panelIndex == 5) return SHP_CHEV_RT;
        if (panelIndex == 4) return SHP_DIAMOND;
        return SHP_FULL; // Corners are always full blocks
    } else {
        // Theme 1: Classic full arcade blocks for everything
        return SHP_FULL;
    }
}

// Resolves the exact RGB values based on the current randomized color theme
void GetThemeColor(int currentColorTheme, float timeState, float panelHueOffset, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (currentColorTheme == 0) {
        // Theme 0: True Global Rainbow (Whole pad cycles through the rainbow together)
        float hue = std::fmod(timeState * 45.0f, 360.0f);
        HSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
    } else if (currentColorTheme == 1) {
        // Theme 1: Offset Cycling Rainbow (Each arrow is a different color, all changing over time)
        float hue = std::fmod((timeState * 45.0f) + panelHueOffset, 360.0f);
        HSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
    } else {
        // Theme 2: Static Neon Colors (No time variance)
        HSVtoRGB(panelHueOffset, 1.0f, 1.0f, r, g, b);
    }
}

void LightingLoop() {
    int renderIdleFrames = 0;
    unsigned short renderLastData = 0xFFFF;
    float timeState = 0.0f; 

    // Seed the randomizer for the themes
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    while(isRunning) {
        unsigned short currentData = latestOutfoxData.load();
        
        // 1. Song Start Detection & Theme Shuffling
        if (currentData != renderLastData) {
            // If we are exiting a menu (idle for > 2 seconds) into active data, a song is starting!
            if (renderIdleFrames > 120) {
                colorTheme = std::rand() % 3;
                shapeTheme = std::rand() % 2;
                effectTheme = std::rand() % 2;
                WriteLog("[THEME] Song Started! Shuffled Themes: Color=" + std::to_string(colorTheme) + 
                         " Shape=" + std::to_string(shapeTheme) + " Effect=" + std::to_string(effectTheme));
            }
            renderLastData = currentData;
            renderIdleFrames = 0;
        } else {
            renderIdleFrames++;
        }

        // 2. Smooth Time Engine
        timeState += 0.016f; 
        bool isIdle = (renderIdleFrames > 120);
        float idleBreath = isIdle ? (std::sin(timeState * 2.5f) + 1.0f) / 2.0f : 0.0f;

        for (int i = 0; i < BUFFER_SIZE; ++i) lightBuffer[i] = 0;

        // 3. Dual-Intensity Render Engine
        for (int pad = 0; pad < 2; ++pad) {
            uint16_t physicalState = SMX_GetInputState(pad);
            
            for (const auto& p : panels) {
                // A. Decay existing intensities
                gameIntensity[pad][p.index] *= 0.85f; 
                physIntensity[pad][p.index] *= 0.75f; // Physical fades slightly faster for a crisp "explosion" feel
                
                // B. Detect new triggers
                if (!isIdle && ((currentData & (1 << p.bit1)) || (currentData & (1 << p.bit2)))) {
                    gameIntensity[pad][p.index] = 1.0f;
                }
                if (physicalState & (1 << p.index)) {
                    physIntensity[pad][p.index] = 1.0f;
                }

                // C. Resolve Shape & Color based on current Theme
                const uint8_t* activeShape = GetPanelShape(p.index, shapeTheme);
                uint8_t r, g, b;
                GetThemeColor(colorTheme, timeState, p.hueOffset, r, g, b);

                int startIdx = (pad * 675) + (p.index * 75);
                if (startIdx + 74 >= BUFFER_SIZE) continue;

                // D. Draw the LEDs
                for (int i = 0; i < 25; ++i) {
                    float ledIntensity = 0.0f;
                    
                    if (isIdle) {
                        // Menu behavior: Gentle breathing of the outline
                        if (activeShape[i] == 1) ledIntensity = 0.10f + (idleBreath * 0.20f);
                    } else {
                        // Gameplay behavior: Mix Game vs Physical based on Effect Theme
                        if (effectTheme == 0) {
                            // "Foot Explosion" Mode: Game lightly pulses the shape, stepping violently flashes it
                            if (activeShape[i] == 1) ledIntensity = 0.15f + (gameIntensity[pad][p.index] * 0.4f);
                            if (physIntensity[pad][p.index] > 0.05f && activeShape[i] == 1) ledIntensity = physIntensity[pad][p.index];
                        } else {
                            // "Flood" Mode: Stepping floods the entire panel (including blank pixels), Game flashes shape
                            if (activeShape[i] == 1) ledIntensity = 0.15f + gameIntensity[pad][p.index];
                            if (physIntensity[pad][p.index] > 0.05f) ledIntensity += physIntensity[pad][p.index] * 0.8f;
                        }
                    }

                    if (ledIntensity > 1.0f) ledIntensity = 1.0f;
                    if (ledIntensity < 0.0f) ledIntensity = 0.0f;

                    lightBuffer[startIdx + (i * 3)]     = static_cast<uint8_t>(r * ledIntensity);
                    lightBuffer[startIdx + (i * 3) + 1] = static_cast<uint8_t>(g * ledIntensity);
                    lightBuffer[startIdx + (i * 3) + 2] = static_cast<uint8_t>(b * ledIntensity);
                }
            }
        }

        if (isSmxInitialized) {
            SMX_SetLights2(reinterpret_cast<const char*>(lightBuffer), BUFFER_SIZE);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); 
    }
}

extern "C" {
    
    __declspec(dllexport) int __stdcall PacInitialize() {
        WriteLog("========================================");
        WriteLog("[PLUGIN] PacInitialize() called. Starting lighting engine...");
        
        if (!isSmxInitialized) {
            SMX_Start(reinterpret_cast<SMXUpdateCallback*>(SMX_Update_Callback), nullptr);
            isSmxInitialized = true;
            
            // Spin up our Zero-Latency Render Thread
            isRunning = true;
            renderThread = std::thread(LightingLoop);
        }
        return 1; 
    }

    __declspec(dllexport) void __stdcall PacShutdown() {
        WriteLog("[PLUGIN] PacShutdown() called. Killing threads...");
        
        isRunning = false;
        if (renderThread.joinable()) {
            renderThread.join();
        }

        if (isSmxInitialized) {
            SMX_Stop();
            isSmxInitialized = false;
        }
        WriteLog("[PLUGIN] Shutdown complete.");
    }

    __declspec(dllexport) bool __stdcall PacSetLEDStates(int id, unsigned short data) {
        if (!isSmxInitialized) return false;

        // The heavy lifting is done in LightingLoop now. Just update the atomic state.
        if (data != lastData) {
            std::string binaryStr = std::bitset<16>(data).to_string();
            WriteLog("[OUTFOX] LED Update -> INT: " + std::to_string(data) + " | BIN: " + binaryStr);
            lastData = data;
            latestOutfoxData = data;
        }
        
        return true;
    }
}