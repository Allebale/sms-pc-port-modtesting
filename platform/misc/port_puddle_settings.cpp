// FLUDD's puddles and the stage's water (port_puddles.h): Puddle Lifetime on the settings menu's
// Graphics page, the rest on its Reflections page, in the order kPuddleSettings gives. Puddle Lifetime: how many
// times longer they last; the game's water asks port_puddle_life_scale every tick
// (zzzzz-ext-24-puddle-lifetime), and Original is the game as it was. Puddle Reflections: characters
// and objects in them too, not only Mario (ext/core/game/port_reflections.cpp), from the next area;
// Mario Only is the game as it was.
//
// The settings feed the reflection options (port_puddle_options.cpp) as they are read and changed;
// a mod may still set them (api->reflections), and they come back when it unloads.
//
// The launcher has no word for it, so it is kept in a file of its own, puddles.txt in the game's
// data folder (beside the memory card's, card-a), and lasts from one game to the next. Its id is
// null: the menu does not try to save it for the launcher.
#include "port_settings.h"
#include "port_puddles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

extern "C" void port_log(const char* fmt, ...);

namespace {

const int kScales[] = {1, 2, 3, 4, 5, 6, 7, 8};
const char* const kNames[] = {"Original", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
const int kCount = int(sizeof kScales / sizeof kScales[0]);

bool s_loaded;
int s_index;       // into kScales: Original until chosen
int s_reflections; // Puddle Reflections: 0 Mario only (as the game has it), 1 everything
int s_surface;     // Reflections by Surface: 0 every puddle alike (as the game has it), 1 by its floor
// The Reflect switches, the Reflections page: what is reflected (port_puddles.h's PORT_REFLECT_*).
struct Kind {
    const char* key;   // in puddles.txt
    const char* name;
    const char* hint;
};
const Kind kKinds[PORT_REFLECT_KINDS] = {
    {"showmario", "Reflect Mario and Yoshi", "Mario, FLUDD and Yoshi in puddles and water."},
    {"showcharacters", "Reflect Characters", "Piantas, Nokis and other people. Needs Puddle Reflections on Everything."},
    {"showenemies", "Reflect Enemies", "Enemies and bosses. Needs Puddle Reflections on Everything."},
    {"showitems", "Reflect Items", "Fruit and other things to take. Needs Puddle Reflections on Everything."},
    {"showobjects", "Reflect Objects", "Crates, trees, doors and the like. Needs Puddle Reflections on Everything."},
};
int s_show[PORT_REFLECT_KINDS] = {1, 0, 0, 0, 0};  // Mario and Yoshi only
int kindIndex(const char* key) {
    for (int i = 0; i < PORT_REFLECT_KINDS; i++)
        if (!strcmp(key, kKinds[i].key)) return i;
    return -1;
}
// A line naming a Reflect switch: read into it.
bool isKind(const char* line, int* value) {
    char key[32];
    if (sscanf(line, " %31[a-z] = %d", key, value) != 2) return false;
    const int i = kindIndex(key);
    if (i < 0) return false;
    s_show[i] = *value != 0;
    return true;
}
int s_ripples = 0; // Water Ripples: 0 off, 1 gentle, 2 strong
int s_puddleRipples = 0; // Puddle Ripples: 0 off, 1 gentle, 2 strong
int s_beatMirrors = 0;    // Puddles Beat Mirrors
int s_footprints = 0;     // Wet Footprints
int s_wetMario = 0;       // Wet Mario
int s_grass = 0;          // 3D Grass: 0 off, 1 by location, 2 always cut, 3 always wild
int s_grassDistance = 2;  // Grass Distance: into kGrassDistances
int s_grassThin = 0;      // Thinned Far Away
int s_grassFlatten = 2;   // Grass Flattening: 0 low .. 4 insane
int s_grassSteps = 5;     // Mario Flattens Grass: 0 off .. 5 always
int s_fireLights = 0;     // Fire Lights
int s_fireReach = 2;      // Fire Light Intensity: into kFireReaches
int s_vegetation = 0;     // Moving Vegetation: 0 off, 1 gentle, 2 breezy, 3 windy
int s_grassCutHeight = 2, s_grassWildHeight = 2;  // into kGrassHeights
int s_shadows = 0;        // Real Shadows: 0 off (the game's own), 1 soft, 2 strong
int s_shadowsRound = 0;   // Round Shadows Under Characters, with Real Shadows: 1 kept (lighter)
int s_shadowsDistance = 2;  // Shadow Distance: into kShadowDistances
int s_shadowsOriginal = 0;  // Keep the Original Shadow Direction
int s_level = 0;          // Reflect the Level: 0 off (as the game has it), 1 the stage itself too
int s_water;       // Water Reflections: 0 none (as the game has it), 1 in the stage's water and wet ground
// 0.5x to 5x the game's 1000 units, in steps of 0.25x (250 units)
const int kDistanceStep = 250;
const int kDistanceCount = (5000 - 500) / kDistanceStep + 1;
const int kDistanceOriginal = (1000 - 500) / kDistanceStep;  // 1000, the game's
int distanceAt(int i) { return 500 + i * kDistanceStep; }
int s_distance = kDistanceOriginal;   // Reflection Distance: into kDistances

// Reflections by Surface's strengths, the Reflections page: tenths of a percent of the game's own, in
// steps of 2.5% (tuned by eye in Delfino Plaza).
struct Surface {
    const char* key;   // in puddles.txt
    const char* name;  // the menu's label
    const char* hint;
    int tenths;        // the default
};
const Surface kSurfaces[PORT_PUDDLE_SURFACES] = {
    {"stone", "Stone and Carpets", "Puddles on stone tiles, carpets and other floors, in percent.", 300},
    {"metal", "Metal", "Puddles on metal roofs and manhole covers, in percent.", 500},
    {"clay", "Clay Roof Tiles", "Puddles on clay roof tiles, in percent.", 400},
    {"wood", "Wood", "Puddles on wooden boardwalks, in percent.", 330},
    {"sand", "Sand", "Puddles on sand, in percent.", 50},
    {"grass", "Grass", "Puddles on grass, in percent.", 60},
    {"canvas", "Canvas", "Puddles on canvas awnings, in percent.", 100},
    {"wet", "Wet Floors", "Puddles on wet, slippery floors, in percent.", 550},
    {"water", "Water", "Sea, lakes, pools, fountains and streams, with Water Reflections, in percent.", 500},
    {"wetground", "Wet Ground", "Wet ground itself, with Water Reflections, in percent.", 450},
};
const int kStep = 10;                       // 1%
const int kSteps = 1000 / kStep + 1;        // 0 to 100%
int s_strength[PORT_PUDDLE_SURFACES];
int s_blur = 0;        // Reflection Blur, in quarters: 0 sharp to 40 (10), 8 is 2
// A Blur in quarters as the menu shows it: "0", "0.25", ... "10".
const char* blurName(int i) {
    static char text[16];
    i = i < 0 ? 0 : i > 40 ? 40 : i;
    if (i % 4) snprintf(text, sizeof text, "%d.%s", i / 4, i % 4 == 1 ? "25" : i % 4 == 2 ? "5" : "75");
    else snprintf(text, sizeof text, "%d", i / 4);
    return text;
}
bool s_strengthSet;
int s_viewAngle = 0;  // Viewing Angle: 0 off, 1 subtle, 2 realistic
int s_glints = 0;     // Sun Glints: 0 off, 1 subtle, 2 bright
int s_depth = 0;      // Water Depth: 0 off, 1 subtle, 2 strong

// "blur = 2.25": to the nearest quarter, within 0 to 10.
bool readBlur(const char* line) {
    float v;
    if (sscanf(line, " blur = %f", &v) != 1) return false;
    int q = int(v * 4.0f + (v < 0 ? -0.5f : 0.5f));
    s_blur = q < 0 ? 0 : q > 40 ? 40 : q;
    return true;
}

void defaultStrengths() {
    if (s_strengthSet) return;
    s_strengthSet = true;
    for (int i = 0; i < PORT_PUDDLE_SURFACES; i++) {
        s_strength[i] = kSurfaces[i].tenths;
    }
}

// ------------------------------------------------------------------ the file
// <data>/sms-port/puddles.txt, where <data> is as the memory card's: SMS_SAVE_DIR's parent when
// set (card-a's folder), else XDG_DATA_HOME, %APPDATA%, or ~/.local/share.
#ifdef _WIN32
std::wstring widen(const char* s) {
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring out(size_t(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s, -1, &out[0], n);
    out.resize(wcslen(out.c_str()));
    return out;
}
std::wstring folder() {
    if (const char* d = getenv("SMS_SAVE_DIR")) {  // .../card-a: its parent
        std::wstring p = widen(d);
        while (!p.empty() && (p.back() == L'/' || p.back() == L'\\')) p.pop_back();
        const size_t slash = p.find_last_of(L"/\\");
        return slash == std::wstring::npos ? L"." : p.substr(0, slash);
    }
    if (const wchar_t* x = _wgetenv(L"APPDATA")) return std::wstring(x) + L"/sms-port";
    return L".";
}
FILE* openFile(const wchar_t* mode) {
    const std::wstring dir = folder();
    for (size_t i = 1; i <= dir.size(); i++)  // make it, a step at a time
        if (i == dir.size() || dir[i] == L'/' || dir[i] == L'\\') _wmkdir(dir.substr(0, i).c_str());
    return _wfopen((dir + L"/puddles.txt").c_str(), mode);
}
#define READ L"r"
#define WRITE L"w"
// The file's path, for the log (UTF-8).
std::string pathForLog() {
    const std::wstring w = folder() + L"/puddles.txt";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n > 0 ? n : 1), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, nullptr, nullptr);
    out.resize(strlen(out.c_str()));
    return out;
}
#else
std::string folder() {
    if (const char* d = getenv("SMS_SAVE_DIR")) {
        std::string p = d;
        while (!p.empty() && p.back() == '/') p.pop_back();
        const size_t slash = p.find_last_of('/');
        return slash == std::string::npos ? "." : p.substr(0, slash);
    }
    if (const char* x = getenv("XDG_DATA_HOME")) return std::string(x) + "/sms-port";
    return std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.local/share/sms-port";
}
FILE* openFile(const char* mode) {
    const std::string dir = folder();
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    return fopen((dir + "/puddles.txt").c_str(), mode);
}
#define READ "r"
#define WRITE "w"
std::string pathForLog() { return folder() + "/puddles.txt"; }
#endif

// The settings in force, for the log.
void logSettings(const char* why) {
    port_log("[puddles] %s: lifetime %dx, reflections %s, by surface %s, distance %d, water %s (%s)\n", why,
             kScales[s_index], s_reflections ? "everything" : "Mario only", s_surface ? "on" : "off",
             distanceAt(s_distance), s_water ? "on" : "off", pathForLog().c_str());
}

void apply();
bool recommendedNow();

void load() {
    if (s_loaded) return;
    s_loaded = true;
    defaultStrengths();
    FILE* f = openFile(READ);
    if (!f) {  // none yet: Original
        logSettings("no settings file yet, so the game's own");
        return;
    }
    char line[128];
    int value;
    bool sawWetMario = false;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, " lifetime = %d", &value) == 1) {
            for (int i = 0; i < kCount; i++)
                if (kScales[i] == value) s_index = i;
        } else if (sscanf(line, " reflections = %d", &value) == 1) {
            s_reflections = value != 0;
        } else if (sscanf(line, " surface = %d", &value) == 1) {
            s_surface = value != 0;
        } else if (readBlur(line)) {
            // done by readBlur
        } else if (sscanf(line, " viewangle = %d", &value) == 1) {
            s_viewAngle = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " glints = %d", &value) == 1) {
            s_glints = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " depth = %d", &value) == 1) {
            s_depth = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (isKind(line, &value)) {
            // done by isKind
        } else if (sscanf(line, " footprints = %d", &value) == 1) {
            s_footprints = value != 0;
        } else if (sscanf(line, " grasscutheight = %d", &value) == 1) {
            s_grassCutHeight = value < 1 ? 1 : value > 5 ? 5 : value;  // no 50 percent for cut grass
        } else if (sscanf(line, " grasswildheight = %d", &value) == 1) {
            s_grassWildHeight = value < 0 ? 0 : value > 5 ? 5 : value;
        } else if (sscanf(line, " vegetation = %d", &value) == 1) {
            s_vegetation = value < 0 ? 0 : value > 3 ? 3 : value;
        } else if (sscanf(line, " firelightreach = %d", &value) == 1) {
            s_fireReach = value < 0 ? 0 : value > 4 ? 4 : value;
        } else if (sscanf(line, " firelights = %d", &value) == 1) {
            s_fireLights = value != 0;
        } else if (sscanf(line, " grasssteps = %d", &value) == 1) {
            s_grassSteps = value < 0 ? 0 : value > 5 ? 5 : value;
        } else if (sscanf(line, " grassflatten = %d", &value) == 1) {
            s_grassFlatten = value < 0 ? 0 : value > 4 ? 4 : value;
        } else if (sscanf(line, " grassthin = %d", &value) == 1) {
            s_grassThin = value != 0;
        } else if (sscanf(line, " grassdistance = %d", &value) == 1) {
            s_grassDistance = value < 0 ? 0 : value > 4 ? 4 : value;
        } else if (sscanf(line, " grassblades = %d", &value) == 1) {
            s_grass = value < 0 ? 0 : value > 3 ? 3 : value;
        } else if (sscanf(line, " characterroundshadows = %d", &value) == 1) {
            s_shadowsRound = value != 0;
        } else if (sscanf(line, " shadowdistance = %d", &value) == 1) {
            s_shadowsDistance = value < 0 ? 0 : value > 4 ? 4 : value;
        } else if (sscanf(line, " shadoworiginaldirection = %d", &value) == 1) {
            s_shadowsOriginal = value != 0;
        } else if (sscanf(line, " shadows = %d", &value) == 1) {
            s_shadows = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " wetmario = %d", &value) == 1) {
            s_wetMario  = value != 0;
            sawWetMario = true;
        } else if (sscanf(line, " level = %d", &value) == 1) {
            s_level = value != 0;
        } else if (sscanf(line, " beatmirrors = %d", &value) == 1) {
            s_beatMirrors = value != 0;
        } else if (sscanf(line, " puddleripples = %d", &value) == 1) {
            s_puddleRipples = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " ripples = %d", &value) == 1) {
            s_ripples = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " waterreflections = %d", &value) == 1) {
            s_water = value != 0;
        } else if (sscanf(line, " distance = %d", &value) == 1) {
            {  // to the nearest step, within 0.5x to 5x
                int i = (value - 500 + kDistanceStep / 2) / kDistanceStep;
                s_distance = i < 0 ? 0 : i >= kDistanceCount ? kDistanceCount - 1 : i;
            }
        } else {
            char key[32];
            float percent;
            if (sscanf(line, " %31[a-z] = %f", key, &percent) == 2)
                for (int i = 0; i < PORT_PUDDLE_SURFACES; i++)
                    if (!strcmp(key, kSurfaces[i].key)) {
                        // to the nearest step, within 0 to 100%
                        int tenths = int(percent * 10.0f + (percent < 0 ? -0.5f : 0.5f));
                        tenths     = (tenths + kStep / 2) / kStep * kStep;
                        s_strength[i] = tenths < 0 ? 0 : tenths > 1000 ? 1000 : tenths;
                    }
        }
    }
    fclose(f);
    // a file from before Wet Mario: on if the rest is Recommended (it stays Recommended), else off
    if (!sawWetMario) {
        s_wetMario = 1;
        if (!recommendedNow()) s_wetMario = 0;
    }
    logSettings("settings read");
    apply();
}

void save() {
    apply();
    FILE* f = openFile(WRITE);
    if (!f) {
        static bool told;
        if (!told) port_log("[puddles] could not save Puddle Lifetime (puddles.txt)\n");
        told = true;
        return;
    }
    fprintf(f, "# FLUDD's puddles (the pause menu's Settings, Graphics).\n");
    fprintf(f, "# lifetime: 1 (Original) to 8 times as long.\n");
    fprintf(f, "lifetime = %d\n", kScales[s_index]);
    fprintf(f, "# reflections: 1 characters and objects in the puddles too, 0 Mario only.\n");
    fprintf(f, "reflections = %d\n", s_reflections);
    fprintf(f, "# surface: 1 puddles reflect more or less by the floor under them, 0 all alike.\n");
    fprintf(f, "surface = %d\n", s_surface);
    fprintf(f, "# distance: how far from Mario reflections are drawn: 500 to 5000 in steps of 250 (1000 is Original).\n");
    fprintf(f, "distance = %d\n", distanceAt(s_distance));
    fprintf(f, "# ripples: the reflection in water wobbles: 0 off, 1 gentle, 2 strong.\n");
    fprintf(f, "ripples = %d\n", s_ripples);
    fprintf(f, "# puddleripples: rings spread in puddles where drops land and Mario runs: 0 off, 1 gentle, 2 strong.\n");
    fprintf(f, "puddleripples = %d\n", s_puddleRipples);
    fprintf(f, "# footprints: 1 Mario leaves damp prints after a puddle, 0 not.\n");
    fprintf(f, "footprints = %d\n", s_footprints);
    fprintf(f, "# wetmario: 1 water leaves Mario wet up to where it reached, dripping, drying over 45 seconds, 0 not.\n");
    fprintf(f, "wetmario = %d\n", s_wetMario);
    fprintf(f, "# grassblades: blades of grass on the grass: 0 off, 1 by location, 2 always cut, 3 always wild.\n");
    fprintf(f, "grassblades = %d\n", s_grass);
    fprintf(f, "# grassdistance: how far the blades go: 0 low, 1 medium, 2 high, 3 very high, 4 max.\n");
    fprintf(f, "grassdistance = %d\n", s_grassDistance);
    fprintf(f, "# grassthin: 1 fewer, wider blades far from Mario (faster), 0 every blade all the way.\n");
    fprintf(f, "grassthin = %d\n", s_grassThin);
    fprintf(f, "# grassflatten: how wide and long FLUDD's spray and Mario's steps flatten the grass: 0 low, 1 medium, 2 high, 3 very high, 4 insane.\n");
    fprintf(f, "grassflatten = %d\n", s_grassFlatten);
    fprintf(f, "# grasssteps: the chance Mario's steps press grass flat: 0 off, 1 10%%, 2 25%%, 3 50%%, 4 75%%, 5 always.\n");
    fprintf(f, "grasssteps = %d\n", s_grassSteps);
    fprintf(f, "# firelights: 1 fires light up what is around them (and in reflections), 0 not.\n");
    fprintf(f, "firelights = %d\n", s_fireLights);
    fprintf(f, "# firelightreach: how far a fire's light goes: 0 very short, 1 short, 2 normal, 3 far, 4 very far.\n");
    fprintf(f, "firelightreach = %d\n", s_fireReach);
    fprintf(f, "# vegetation: plants sway, bend from Mario and FLUDD's water: 0 off, 1 gentle, 2 breezy, 3 windy.\n");
    fprintf(f, "vegetation = %d\n", s_vegetation);
    fprintf(f, "# grasscutheight, grasswildheight: 0 50%% (wild only), 1 75%%, 2 100%%, 3 150%%, 4 200%%, 5 300%%.\n");
    fprintf(f, "grasscutheight = %d\n", s_grassCutHeight);
    fprintf(f, "grasswildheight = %d\n", s_grassWildHeight);
    fprintf(f, "# shadows: the sun's shadows of everything: 0 off (the game's own), 1 soft, 2 strong.\n");
    fprintf(f, "shadows = %d\n", s_shadows);
    fprintf(f, "# characterroundshadows: with shadows on, 1 the game's round shadows under characters kept (lighter), 0 not.\n");
    fprintf(f, "characterroundshadows = %d\n", s_shadowsRound);
    fprintf(f, "# shadowdistance: how far the shadows reach: 0 short, 1 medium, 2 long, 3 very long, 4 max.\n");
    fprintf(f, "shadowdistance = %d\n", s_shadowsDistance);
    fprintf(f, "# shadoworiginaldirection: 1 the shadows fall where the game's own did, 0 from the sun in the sky.\n");
    fprintf(f, "shadoworiginaldirection = %d\n", s_shadowsOriginal);
    fprintf(f, "# level: 1 the stage itself (ground, buildings, sky) in puddles and water too, 0 not.\n");
    fprintf(f, "level = %d\n", s_level);
    fprintf(f, "# beatmirrors: 1 puddles near Mario take the reflection from a real mirror's area, 0 not.\n");
    fprintf(f, "beatmirrors = %d\n", s_beatMirrors);
    fprintf(f, "# viewangle: reflections by how slanting the view is: 0 off, 1 subtle, 2 realistic.\n");
    fprintf(f, "viewangle = %d\n", s_viewAngle);
    fprintf(f, "# glints: the sun's light off puddles and water: 0 off, 1 subtle, 2 bright.\n");
    fprintf(f, "glints = %d\n", s_glints);
    fprintf(f, "# depth: water darker over deep ground: 0 off, 1 subtle, 2 strong.\n");
    fprintf(f, "depth = %d\n", s_depth);
    fprintf(f, "# blur: how blurred reflections in puddles and water are, 0 sharp to 10 in steps of 0.25.\n");
    fprintf(f, "blur = %s\n", blurName(s_blur));
    fprintf(f, "# What is reflected (the menu's Reflections page): 1 on, 0 off.\n");
    for (int i = 0; i < PORT_REFLECT_KINDS; i++) fprintf(f, "%s = %d\n", kKinds[i].key, s_show[i]);
    fprintf(f, "# waterreflections: 1 the stage's water and wet ground reflect as puddles do, 0 not.\n");
    fprintf(f, "waterreflections = %d\n", s_water);
    fprintf(f, "# How strongly puddles reflect on each surface, in percent (the menu's Reflections page).\n");
    for (int i = 0; i < PORT_PUDDLE_SURFACES; i++)
        fprintf(f, "%s = %d.%d\n", kSurfaces[i].key, s_strength[i] / 10, s_strength[i] % 10);
    fclose(f);
}

const char* const kGrassNames[] = {"Off", "By Location", "Always Cut", "Always Wild"};
int grassCount() { return 4; }
const char* grassName(int i) { return kGrassNames[i < 0 || i > 3 ? 0 : i]; }
int grassGet() {
    load();
    return s_grass;
}
int grassSet(int i) {
    if (i < 0 || i > 3) return 0;
    load();
    s_grass = i;
    save();
    port_log("[grass] 3D Grass changed: %s\n", kGrassNames[i]);
    return 1;
}

const char* const kGrassDistanceNames[] = {"Low", "Medium", "High", "Very High", "Max"};
const float kGrassDistances[] = {1.0f, 2.0f, 4.0f, 7.0f, 12.0f};
int grassDistanceCount() { return 5; }
const char* grassDistanceName(int i) { return kGrassDistanceNames[i < 0 || i > 4 ? 2 : i]; }
int grassDistanceGet() {
    load();
    return s_grassDistance;
}
int grassDistanceSet(int i) {
    if (i < 0 || i > 4) return 0;
    load();
    s_grassDistance = i;
    save();
    port_log("[grass] Grass Distance changed: %s\n", kGrassDistanceNames[i]);
    return 1;
}

const char* const kGrassHeightNames[] = {"50 percent", "75 percent", "Normal", "150 percent", "Twice", "Three Times"};
const float kGrassHeights[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
// The menu's rows: cut grass has no 50 percent (too short to see), so its first is 75 percent.
template <int Wild>
int grassHeightCount() { return Wild ? 6 : 5; }
template <int Wild>
const char* grassHeightName(int i) {
    i += Wild ? 0 : 1;
    return kGrassHeightNames[i < 0 || i > 5 ? 2 : i];
}
template <int Wild>
int grassHeightGet() {
    load();
    return Wild ? s_grassWildHeight : s_grassCutHeight - 1;
}
template <int Wild>
int grassHeightSet(int i) {
    i += Wild ? 0 : 1;
    if (i < (Wild ? 0 : 1) || i > 5) return 0;
    load();
    (Wild ? s_grassWildHeight : s_grassCutHeight) = i;
    save();
    port_log("[grass] %s Grass Height changed: %s\n", Wild ? "Wild" : "Cut", kGrassHeightNames[i]);
    return 1;
}

const char* const kFireReachNames[] = {"Very Short", "Short", "Normal", "Far", "Very Far"};
const float kFireReaches[] = {0.4f, 0.7f, 1.0f, 1.6f, 2.5f};
int fireReachCount() { return 5; }
const char* fireReachName(int i) { return kFireReachNames[i < 0 || i > 4 ? 2 : i]; }
int fireReachGet() {
    load();
    return s_fireReach;
}
int fireReachSet(int i) {
    if (i < 0 || i > 4) return 0;
    load();
    s_fireReach = i;
    save();
    port_log("[lights] Fire Light Intensity changed: %s\n", kFireReachNames[i]);
    return 1;
}

const char* const kVegetationNames[] = {"Off", "Gentle", "Breezy", "Windy"};
int vegetationCount() { return 4; }
const char* vegetationName(int i) { return kVegetationNames[i < 0 || i > 3 ? 0 : i]; }
int vegetationGet() {
    load();
    return s_vegetation;
}
int vegetationSet(int i) {
    if (i < 0 || i > 3) return 0;
    load();
    s_vegetation = i;
    save();
    port_log("[vegetation] Moving Vegetation changed: %s\n", kVegetationNames[i]);
    return 1;
}

int fireLightsGet() {
    load();
    return s_fireLights;
}
int fireLightsSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_fireLights = i;
    save();
    port_log("[lights] Fire Lights changed: %s\n", i ? "on" : "off");
    return 1;
}

const char* const kGrassFlattenNames[] = {"Low", "Medium", "High", "Very High", "Insane"};
int grassFlattenCount() { return 5; }
const char* grassFlattenName(int i) { return kGrassFlattenNames[i < 0 || i > 4 ? 2 : i]; }
int grassFlattenGet() {
    load();
    return s_grassFlatten;
}
int grassFlattenSet(int i) {
    if (i < 0 || i > 4) return 0;
    load();
    s_grassFlatten = i;
    save();
    port_log("[grass] Grass Flattening changed: %s\n", kGrassFlattenNames[i]);
    return 1;
}

const char* const kGrassStepNames[] = {"Off", "10 percent", "25 percent", "50 percent", "75 percent", "Always"};
int grassStepsCount() { return 6; }
const char* grassStepsName(int i) { return kGrassStepNames[i < 0 || i > 5 ? 5 : i]; }
int grassStepsGet() {
    load();
    return s_grassSteps;
}
int grassStepsSet(int i) {
    if (i < 0 || i > 5) return 0;
    load();
    s_grassSteps = i;
    save();
    port_log("[grass] Mario Flattens Grass changed: %s\n", kGrassStepNames[i]);
    return 1;
}

int grassThinGet() {
    load();
    return s_grassThin;
}
int grassThinSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_grassThin = i;
    save();
    port_log("[grass] Thinned Far Away changed: %s\n", i ? "on" : "off");
    return 1;
}

const char* const kShadowNames[] = {"Off", "Soft", "Strong"};
int shadowsCount() { return 3; }
const char* shadowsName(int i) { return kShadowNames[i < 0 || i > 2 ? 0 : i]; }
int shadowsGet() {
    load();
    return s_shadows;
}
int shadowsSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_shadows = i;
    save();
    port_log("[shadows] Real Shadows changed: %s\n", kShadowNames[i]);
    return 1;
}

const char* const kShadowDistanceNames[] = {"Short", "Medium", "Long", "Very Long", "Max"};
const float kShadowDistances[] = {4000.0f, 8000.0f, 16000.0f, 24000.0f, 32000.0f};
int distanceCount5() { return 5; }
const char* shadowDistanceName(int i) { return kShadowDistanceNames[i < 0 || i > 4 ? 2 : i]; }
int shadowDistanceGet() {
    load();
    return s_shadowsDistance;
}
int shadowDistanceSet(int i) {
    if (i < 0 || i > 4) return 0;
    load();
    s_shadowsDistance = i;
    save();
    port_log("[shadows] Shadow Distance changed: %s\n", kShadowDistanceNames[i]);
    return 1;
}
int originalGet() {
    load();
    return s_shadowsOriginal;
}
int originalSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_shadowsOriginal = i;
    save();
    port_log("[shadows] Keep the Original Shadow Direction changed: %s\n", i ? "on" : "off");
    return 1;
}

int roundGet() {
    load();
    return s_shadowsRound;
}
int roundSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_shadowsRound = i;
    save();
    port_log("[shadows] Round Shadows Under Characters changed: %s\n", i ? "on" : "off");
    return 1;
}

int count() { return kCount; }
const char* name(int i) { return i >= 0 && i < kCount ? kNames[i] : kNames[0]; }
int get() {
    load();
    return s_index;
}
int set(int i) {
    if (i < 0 || i >= kCount) return 0;
    load();
    s_index = i;
    save();
    logSettings("Puddle Lifetime changed");
    return 1;
}

int reflectionsCount() { return 2; }
const char* reflectionsName(int i) { return i ? "Everything" : "Mario Only"; }
int reflectionsGet() {
    load();
    return s_reflections;
}
int reflectionsSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_reflections = i;
    save();
    logSettings("Puddle Reflections changed");
    return 1;
}

int offOnCount() { return 2; }
const char* offOnName(int i) { return i ? "On" : "Off"; }
int surfaceGet() {
    load();
    return s_surface;
}
int surfaceSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_surface = i;
    save();
    logSettings("Reflections by Surface changed");
    return 1;
}

int waterGet() {
    load();
    return s_water;
}
int waterSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_water = i;
    save();
    logSettings("Water Reflections changed");
    return 1;
}

int ripplesCount() { return 3; }
const char* ripplesName(int i) { return i == 2 ? "Strong" : i == 1 ? "Gentle" : "Off"; }
int ripplesGet() {
    load();
    return s_ripples;
}
int ripplesSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_ripples = i;
    save();
    port_log("[puddles] Water Ripples changed: %s\n", ripplesName(i));
    return 1;
}

int puddleRipplesGet() {
    load();
    return s_puddleRipples;
}
int puddleRipplesSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_puddleRipples = i;
    save();
    port_log("[puddles] Puddle Ripples changed: %s\n", ripplesName(i));
    return 1;
}

int footprintsGet() {
    load();
    return s_footprints;
}
int footprintsSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_footprints = i;
    save();
    port_log("[puddles] Wet Footprints changed: %s\n", i ? "on" : "off");
    return 1;
}
int wetMarioGet() {
    load();
    return s_wetMario;
}
int wetMarioSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_wetMario = i;
    save();
    port_log("[puddles] Wet Mario changed: %s\n", i ? "on" : "off");
    return 1;
}
int levelGet() {
    load();
    return s_level;
}
int levelSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_level = i;
    save();
    port_log("[puddles] Reflect the Level changed: %s (from the next area)\n", i ? "on" : "off");
    return 1;
}

int beatGet() {
    load();
    return s_beatMirrors;
}
int beatSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_beatMirrors = i;
    save();
    port_log("[puddles] Puddles Beat Mirrors changed: %s\n", i ? "on" : "off");
    return 1;
}

int levelCount3() { return 3; }
const char* viewName(int i) { return i == 2 ? "Realistic" : i == 1 ? "Subtle" : "Off"; }
const char* glintName(int i) { return i == 2 ? "Bright" : i == 1 ? "Subtle" : "Off"; }
const char* depthName(int i) { return i == 2 ? "Strong" : i == 1 ? "Subtle" : "Off"; }
int viewGet() { load(); return s_viewAngle; }
int viewSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_viewAngle = i;
    save();
    port_log("[puddles] Viewing Angle changed: %s\n", viewName(i));
    return 1;
}
int glintGet() { load(); return s_glints; }
int glintSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_glints = i;
    save();
    port_log("[puddles] Sun Glints changed: %s\n", glintName(i));
    return 1;
}
int depthGet() { load(); return s_depth; }
int depthSet(int i) {
    if (i < 0 || i > 2) return 0;
    load();
    s_depth = i;
    save();
    port_log("[puddles] Water Depth changed: %s\n", depthName(i));
    return 1;
}

int blurCount() { return 41; }  // 0 to 10 in steps of 0.25
int blurGet() {
    load();
    return s_blur;
}
int blurSet(int i) {
    if (i < 0 || i > 40) return 0;
    load();
    s_blur = i;
    save();
    port_log("[puddles] Reflection Blur: %s\n", blurName(i));
    return 1;
}

// ------------------------------------------------------------------ the preset
// One row that sets every reflection option at once: Original (the game as it was, these options'
// defaults) or Recommended (the mod's own pick). It reads Custom when the options are neither.
// Puddle Lifetime, on the Graphics page, is not part of it.
struct Preset {
    int reflections, water, distanceTimes4, beatMirrors, level;
    int viewAngle, puddleRipples, ripples, glints, depth, blurQuarters, footprints, wetMario;
    int show[PORT_REFLECT_KINDS];
    int surface;
    int strength[PORT_PUDDLE_SURFACES];  // tenths of a percent
};
const Preset kPresets[2] = {
    // Original
    {0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {1, 0, 0, 0, 0}, 0, {300, 500, 400, 330, 50, 60, 100, 550, 500, 450}},
    // Recommended
    {1, 1, 20, 1, 1, 2, 2, 2, 1, 1, 5, 1, 1, {1, 1, 1, 1, 1}, 1, {300, 500, 400, 330, 50, 60, 100, 550, 500, 450}},
};
const char* const kPresetNames[] = {"Original", "Recommended", "Custom"};

int distanceIndexOf(int times4) {  // 4 is 1x
    int i = (times4 * 250 - 500) / kDistanceStep;
    return i < 0 ? 0 : i >= kDistanceCount ? kDistanceCount - 1 : i;
}

bool presetIs(const Preset& p) {
    if (s_reflections != p.reflections || s_water != p.water || s_distance != distanceIndexOf(p.distanceTimes4)
        || s_beatMirrors != p.beatMirrors || s_level != p.level || s_viewAngle != p.viewAngle
        || s_puddleRipples != p.puddleRipples || s_ripples != p.ripples || s_glints != p.glints
        || s_depth != p.depth || s_blur != p.blurQuarters || s_footprints != p.footprints || s_wetMario != p.wetMario
        || s_surface != p.surface)
        return false;
    for (int i = 0; i < PORT_REFLECT_KINDS; i++)
        if (s_show[i] != p.show[i]) return false;
    for (int i = 0; i < PORT_PUDDLE_SURFACES; i++)
        if (s_strength[i] != p.strength[i]) return false;
    return true;
}

bool recommendedNow() { return presetIs(kPresets[1]); }

int presetCount() { return 3; }
const char* presetName(int i) { return kPresetNames[i < 0 || i > 2 ? 2 : i]; }
int presetGet() {
    load();
    return presetIs(kPresets[0]) ? 0 : presetIs(kPresets[1]) ? 1 : 2;
}
int presetSet(int i) {
    if (i < 0 || i > 1) return 1;  // Custom changes nothing, as on the Graphics page
    load();
    const Preset& p = kPresets[i];
    s_reflections   = p.reflections;
    s_water         = p.water;
    s_distance      = distanceIndexOf(p.distanceTimes4);
    s_beatMirrors   = p.beatMirrors;
    s_level         = p.level;
    s_viewAngle     = p.viewAngle;
    s_puddleRipples = p.puddleRipples;
    s_ripples       = p.ripples;
    s_glints        = p.glints;
    s_depth         = p.depth;
    s_blur          = p.blurQuarters;
    s_footprints    = p.footprints;
    s_wetMario      = p.wetMario;
    s_surface       = p.surface;
    for (int k = 0; k < PORT_REFLECT_KINDS; k++) s_show[k] = p.show[k];
    for (int k = 0; k < PORT_PUDDLE_SURFACES; k++) s_strength[k] = p.strength[k];
    save();
    logSettings(i ? "Reflections set to Recommended" : "Reflections set to Original");
    return 1;
}

template <int N>
int showGet() {
    load();
    return s_show[N];
}
template <int N>
int showSet(int i) {
    if (i < 0 || i > 1) return 0;
    load();
    s_show[N] = i;
    save();
    port_log("[puddles] %s: %s\n", kKinds[N].name, i ? "on" : "off");
    return 1;
}

int distanceCount() { return kDistanceCount; }
// "0.5x", "0.75x", "Original", "1.25x", ... "5x"
const char* distanceName(int i) {
    static char text[16];
    i = i < 0 ? 0 : i >= kDistanceCount ? kDistanceCount - 1 : i;
    if (i == kDistanceOriginal) return "Original";
    const int quarters = distanceAt(i) / 250;  // in quarters of 1x
    const int whole = quarters / 4, rest = quarters % 4;
    if (!rest) snprintf(text, sizeof text, "%dx", whole);
    else snprintf(text, sizeof text, "%d.%sx", whole, rest == 1 ? "25" : rest == 2 ? "5" : "75");
    return text;
}
int distanceGet() {
    load();
    return s_distance;
}
int distanceSet(int i) {
    if (i < 0 || i >= kDistanceCount) return 0;
    load();
    s_distance = i;
    save();
    logSettings("Reflection Distance changed");
    return 1;
}

// A surface's strength: its value is the step (0 is 0%, 3 is 7.5%, 40 is 100%).
int strengthCount() { return kSteps; }
const char* strengthName(int i) {
    static char text[16];
    const int tenths = (i < 0 ? 0 : i >= kSteps ? kSteps - 1 : i) * kStep;
    if (tenths % 10)
        snprintf(text, sizeof text, "%d.%d", tenths / 10, tenths % 10);
    else
        snprintf(text, sizeof text, "%d", tenths / 10);
    return text;
}
template <int N>
int strengthGet() {
    load();
    return s_strength[N] / kStep;
}
template <int N>
int strengthSet(int i) {
    if (i < 0 || i >= kSteps) return 0;
    load();
    s_strength[N] = i * kStep;
    save();
    port_log("[puddles] %s: %s percent\n", kSurfaces[N].name, strengthName(i));
    return 1;
}
#define SHOW_ROW(N, ORDER)                                                                              \
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, ORDER, kKinds[N].name, kKinds[N].hint, 0, nullptr, offOnCount, offOnName, \
     showGet<N>, showSet<N>}
#define SURFACE_ROW(N, ORDER)                                                                           \
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, ORDER, kSurfaces[N].name, kSurfaces[N].hint, 0, nullptr, strengthCount, \
     strengthName, strengthGet<N>, strengthSet<N>}

// Name and hint: plain letters, digits, spaces and . , - ( ) ' only (port_settings.h).
const PortSetting kPuddleSettings[] = {
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 95, "Puddle Lifetime",
     "How long FLUDD's puddles last. Goop and objects react as in the original game.", 0, nullptr, count,
     name, get, set},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 100, "3D Grass",
     "Blades of grass, cut short in town, wild in the hills. They sway and bend round Mario.", 0, nullptr,
     grassCount, grassName, grassGet, grassSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 101, "Grass Distance",
     "How far from Mario the blades of grass go. Farther costs a little more.", 0, nullptr,
     grassDistanceCount, grassDistanceName, grassDistanceGet, grassDistanceSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 102, "Grass Thinned Far Away",
     "Fewer, wider blades far from Mario: faster, but they show as you come near.", 0, nullptr,
     offOnCount, offOnName, grassThinGet, grassThinSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 103, "Cut Grass Height",
     "How tall the cut and tended grass grows (town lawns, parks).", 0, nullptr, grassHeightCount<0>,
     grassHeightName<0>, grassHeightGet<0>, grassHeightSet<0>},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 104, "Wild Grass Height",
     "How tall the wild grass grows (hills, villages, beaches).", 0, nullptr, grassHeightCount<1>,
     grassHeightName<1>, grassHeightGet<1>, grassHeightSet<1>},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 105, "Grass Flattening",
     "How wide and how long FLUDD's spray and Mario's steps press the grass flat.", 0, nullptr,
     grassFlattenCount, grassFlattenName, grassFlattenGet, grassFlattenSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 106, "Mario Flattens Grass",
     "The chance each of Mario's steps on the grass presses it flat.", 0, nullptr, grassStepsCount,
     grassStepsName, grassStepsGet, grassStepsSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 108, "Fire Lights",
     "Fires light up the ground and walls around them, in reflections too.", 0, nullptr, offOnCount,
     offOnName, fireLightsGet, fireLightsSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 109, "Fire Light Intensity",
     "How far a fire's light reaches round it, with Fire Lights on.", 0, nullptr, fireReachCount,
     fireReachName, fireReachGet, fireReachSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 107, "Moving Vegetation",
     "Palms, bushes and flowers sway in the wind and bend away from Mario and FLUDD's water.", 0,
     nullptr, vegetationCount, vegetationName, vegetationGet, vegetationSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 96, "Real Shadows",
     "The sun's shadows of everything, the level and characters too. Off is the original.", 0,
     nullptr, shadowsCount, shadowsName, shadowsGet, shadowsSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 97, "Round Shadows Under Characters",
     "With Real Shadows, the game's own round shadow under each character too, lighter.", 0, nullptr,
     offOnCount, offOnName, roundGet, roundSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 98, "Shadow Distance",
     "How far from Mario Real Shadows reach. Farther is a little softer far away.", 0, nullptr,
     distanceCount5, shadowDistanceName, shadowDistanceGet, shadowDistanceSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 99, "Keep the Original Shadow Direction",
     "Real Shadows fall exactly where the game's own did. Off, from the sun in the sky.", 0, nullptr,
     offOnCount, offOnName, originalGet, originalSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 0, "Preset",
     "The original game, or the recommended look. Some apply from the next area.", 1, nullptr, presetCount,
     presetName, presetGet, presetSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 1, "Puddle Reflections",
     "Characters and objects in FLUDD's puddles too, from the next area.", 1, nullptr, reflectionsCount,
     reflectionsName, reflectionsGet, reflectionsSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 30, "Reflections by Surface",
     "Puddles reflect more or less by surface. Set each surface below.", 0, nullptr, offOnCount, offOnName,
     surfaceGet, surfaceSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 3, "Reflection Distance",
     "How far from Mario puddles show reflections, fading out at the edge.", 0, nullptr, distanceCount,
     distanceName, distanceGet, distanceSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 2, "Water Reflections",
     "The sea, pools, fountains, streams and wet ground reflect as puddles do.", 0, nullptr, offOnCount,
     offOnName, waterGet, waterSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 12, "Water Ripples",
     "The reflection in water wobbles with moving waves. Puddles stay still.", 0, nullptr, ripplesCount,
     ripplesName, ripplesGet, ripplesSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 11, "Puddle Ripples",
     "New puddles and Mario's steps ripple the reflection, calming over 2 seconds.", 0, nullptr, ripplesCount,
     ripplesName, puddleRipplesGet, puddleRipplesSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 17, "Wet Footprints",
     "After a puddle, Mario's steps leave damp prints that dry away.", 0, nullptr, offOnCount, offOnName,
     footprintsGet, footprintsSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 18, "Wet Mario",
     "Water leaves Mario wet up to where it reached, darker and dripping as he dries.", 0, nullptr,
     offOnCount, offOnName, wetMarioGet, wetMarioSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 5, "Reflect the Level",
     "Buildings, trees and the sky in puddles and water too. From the next area.", 1, nullptr, offOnCount,
     offOnName, levelGet, levelSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 4, "Puddles Beat Mirrors",
     "Near a puddle in a mirror's area, the puddle shows reflections, not the mirror.", 0, nullptr, offOnCount,
     offOnName, beatGet, beatSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 10, "Viewing Angle",
     "Reflections stronger looking across puddles and water, weaker looking down.", 0, nullptr, levelCount3,
     viewName, viewGet, viewSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 13, "Sun Glints",
     "The sun's light sparkles off puddles and water. None indoors.", 0, nullptr, levelCount3, glintName,
     glintGet, glintSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 15, "Reflection Blur",
     "How blurred reflections in puddles and water are, 0 sharp to 10.", 0, nullptr, blurCount, blurName,
     blurGet, blurSet},
    {nullptr, PORT_SETTINGS_PAGE_REFLECTIONS, 14, "Water Depth",
     "The sea, lakes and pools darken toward blue-green over deep ground.", 0, nullptr, levelCount3, depthName,
     depthGet, depthSet},
    SHOW_ROW(PORT_REFLECT_MARIO, 20),
    SHOW_ROW(PORT_REFLECT_CHARACTERS, 21),
    SHOW_ROW(PORT_REFLECT_ENEMIES, 22),
    SHOW_ROW(PORT_REFLECT_ITEMS, 23),
    SHOW_ROW(PORT_REFLECT_OBJECTS, 24),
    SURFACE_ROW(PORT_PUDDLE_STONE, 40),
    SURFACE_ROW(PORT_PUDDLE_METAL, 41),
    SURFACE_ROW(PORT_PUDDLE_CLAY, 42),
    SURFACE_ROW(PORT_PUDDLE_WOOD, 43),
    SURFACE_ROW(PORT_PUDDLE_SAND, 44),
    SURFACE_ROW(PORT_PUDDLE_GRASS, 45),
    SURFACE_ROW(PORT_PUDDLE_CANVAS, 46),
    SURFACE_ROW(PORT_PUDDLE_WET, 47),
    SURFACE_ROW(PORT_PUDDLE_WATER, 48),
    SURFACE_ROW(PORT_PUDDLE_WET_GROUND, 49),
};

// ------------------------------------------------------------------ to the game
// The settings feed the reflection options (port_puddle_options.cpp, the same the mod API's
// api->reflections sets), in their units.
void apply() {
    port_reflect_option_set(PORT_OPT_LIFETIME, kScales[s_index]);
    port_reflect_option_set(PORT_OPT_PUDDLES, s_reflections);
    port_reflect_option_set(PORT_OPT_DISTANCE, distanceAt(s_distance));
    port_reflect_option_set(PORT_OPT_WATER, s_water);
    port_reflect_option_set(PORT_OPT_BEAT_MIRRORS, s_beatMirrors);
    port_reflect_option_set(PORT_OPT_LEVEL, s_level);
    port_reflect_option_set(PORT_OPT_VIEW_ANGLE, s_viewAngle);
    port_reflect_option_set(PORT_OPT_PUDDLE_RIPPLES, s_puddleRipples);
    port_reflect_option_set(PORT_OPT_WATER_RIPPLES, s_ripples);
    port_reflect_option_set(PORT_OPT_SUN_GLINTS, s_glints);
    port_reflect_option_set(PORT_OPT_WATER_DEPTH, s_depth);
    port_reflect_option_set(PORT_OPT_BLUR, s_blur * 25);  // quarters to hundredths
    port_reflect_option_set(PORT_OPT_FOOTPRINTS, s_footprints);
    port_reflect_option_set(PORT_OPT_WET_MARIO, s_wetMario);
    for (int k = 0; k < PORT_REFLECT_KINDS; k++) port_reflect_option_set(PORT_OPT_SHOW + k, s_show[k]);
    port_reflect_option_set(PORT_OPT_BY_SURFACE, s_surface);
    for (int k = 0; k < PORT_PUDDLE_SURFACES; k++) port_reflect_option_set(PORT_OPT_STRENGTH + k, s_strength[k]);
}

}  // namespace

extern "C" {

const PortSetting* port_puddle_settings(int* n) {
    *n = int(sizeof kPuddleSettings / sizeof kPuddleSettings[0]);
    return kPuddleSettings;
}

// 3D Grass: 0 off, 1 by location, 2 always cut, 3 always wild (ext/core/game/port_grass.cpp).
int port_grass(void) {
    load();
    return s_grass;
}
float port_grass_height(int wild) {
    load();
    const int i = wild ? s_grassWildHeight : s_grassCutHeight;
    return kGrassHeights[i < 0 || i > 5 ? 2 : i];
}
// Moving Vegetation (ext/core/game/port_vegetation.cpp): 0 off, 1 gentle, 2 breezy, 3 windy.
int port_vegetation(void) {
    load();
    return s_vegetation;
}
// Fire Light Intensity: how far a fire's light reaches, times the normal reach.
float port_fire_light_reach(void) {
    load();
    return kFireReaches[s_fireReach < 0 || s_fireReach > 4 ? 2 : s_fireReach];
}
// Fire Lights (ext/core/game/port_lights.cpp): 1 on.
int port_fire_lights(void) {
    load();
    return s_fireLights;
}
int port_grass_step_chance(void) {
    load();
    return s_grassSteps;
}
int port_grass_flatten(void) {
    load();
    return s_grassFlatten;
}
int port_grass_thin(void) {
    load();
    return s_grassThin;
}
float port_grass_distance(void) {
    load();
    return kGrassDistances[s_grassDistance < 0 || s_grassDistance > 4 ? 2 : s_grassDistance];
}

// Real Shadows: 0 off, 1 soft, 2 strong (ext/core/game/port_shadows.cpp, each frame).
int port_shadows(void) {
    load();
    return s_shadows;
}
int port_shadows_round(void) {
    load();
    return s_shadowsRound;
}
float port_shadows_distance(void) {
    load();
    return kShadowDistances[s_shadowsDistance < 0 || s_shadowsDistance > 4 ? 2 : s_shadowsDistance];
}
int port_shadows_original_direction(void) {
    load();
    return s_shadowsOriginal;
}

// The options as the settings have them (port_puddle_options.cpp asks as it starts, and again
// when a mod that set any unloads).
void port_puddle_settings_apply(void) {
    load();
    apply();
}

}  // extern "C"
