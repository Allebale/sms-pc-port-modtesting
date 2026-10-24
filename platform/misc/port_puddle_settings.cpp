// FLUDD's puddles, on the settings menu's Graphics page (port_puddles.h). Puddle Lifetime: how many
// times longer they last; the game's water asks port_puddle_life_scale every tick
// (zzzzz-ext-24-puddle-lifetime), and Original is the game as it was. Puddle Reflections: characters
// and objects in them too, not only Mario (ext/core/game/port_reflections.cpp), from the next area;
// Mario Only is the game as it was.
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

const int kScales[] = {1, 2, 4, 8};
const char* const kNames[] = {"Original", "2x", "4x", "8x"};
const int kCount = int(sizeof kScales / sizeof kScales[0]);

bool s_loaded;
int s_index;       // into kScales: Original until chosen
int s_reflections; // Puddle Reflections: 0 Mario only (as the game has it), 1 everything
int s_surface;     // Reflections by Surface: 0 every puddle alike (as the game has it), 1 by its floor
// The Reflect switches, the Other page: what is reflected (port_puddles.h's PORT_REFLECT_*).
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
int s_show[PORT_REFLECT_KINDS] = {1, 1, 1, 1, 1};
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
int s_ripples = 1; // Water Ripples: 0 off, 1 gentle, 2 strong
int s_water;       // Water Reflections: 0 none (as the game has it), 1 in the stage's water and wet ground
const int kDistances[] = {500, 750, 1000, 2000, 3000, 5000};
const char* const kDistanceNames[] = {"0.5x", "0.75x", "Original", "2x", "3x", "5x"};
const int kDistanceCount = int(sizeof kDistances / sizeof kDistances[0]);
const int kDistanceOriginal = 2;      // 1000, the game's
int s_distance = kDistanceOriginal;   // Reflection Distance: into kDistances

// Reflections by Surface's strengths, the Other page: tenths of a percent of the game's own, in
// steps of 2.5% (tuned by eye in Delfino Plaza).
struct Surface {
    const char* key;   // in puddles.txt
    const char* name;  // the menu's label
    const char* hint;
    int tenths;        // the default
};
const Surface kSurfaces[PORT_PUDDLE_SURFACES] = {
    {"stone", "Stone and Carpets", "Puddles on stone tiles, carpets and other floors, in percent.", 450},
    {"metal", "Metal", "Puddles on metal roofs and manhole covers, in percent.", 500},
    {"clay", "Clay Roof Tiles", "Puddles on clay roof tiles, in percent.", 400},
    {"wood", "Wood", "Puddles on wooden boardwalks, in percent.", 325},
    {"sand", "Sand", "Puddles on sand, in percent.", 100},
    {"grass", "Grass", "Puddles on grass, in percent.", 75},
    {"canvas", "Canvas", "Puddles on canvas awnings, in percent.", 100},
    {"wet", "Wet Floors", "Puddles on wet, slippery floors, in percent.", 550},
    {"water", "Water", "Sea, lakes, pools, fountains and streams, with Water Reflections, in percent.", 500},
    {"wetground", "Wet Ground", "Wet ground itself, with Water Reflections, in percent.", 400},
};
const int kStep = 25;                       // 2.5%
const int kSteps = 1000 / kStep + 1;        // 0 to 100%
int s_strength[PORT_PUDDLE_SURFACES];
bool s_strengthSet;

void defaultStrengths() {
    if (s_strengthSet) return;
    s_strengthSet = true;
    for (int i = 0; i < PORT_PUDDLE_SURFACES; i++) s_strength[i] = kSurfaces[i].tenths;
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
             kDistances[s_distance], s_water ? "on" : "off", pathForLog().c_str());
}

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
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, " lifetime = %d", &value) == 1) {
            for (int i = 0; i < kCount; i++)
                if (kScales[i] == value) s_index = i;
        } else if (sscanf(line, " reflections = %d", &value) == 1) {
            s_reflections = value != 0;
        } else if (sscanf(line, " surface = %d", &value) == 1) {
            s_surface = value != 0;
        } else if (isKind(line, &value)) {
            // done by isKind
        } else if (sscanf(line, " ripples = %d", &value) == 1) {
            s_ripples = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " waterreflections = %d", &value) == 1) {
            s_water = value != 0;
        } else if (sscanf(line, " distance = %d", &value) == 1) {
            for (int i = 0; i < kDistanceCount; i++)
                if (kDistances[i] == value) s_distance = i;
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
    logSettings("settings read");
}

void save() {
    FILE* f = openFile(WRITE);
    if (!f) {
        static bool told;
        if (!told) port_log("[puddles] could not save Puddle Lifetime (puddles.txt)\n");
        told = true;
        return;
    }
    fprintf(f, "# FLUDD's puddles (the pause menu's Settings, Graphics).\n");
    fprintf(f, "# lifetime: 1 (Original), 2, 4 or 8 times as long.\n");
    fprintf(f, "lifetime = %d\n", kScales[s_index]);
    fprintf(f, "# reflections: 1 characters and objects in the puddles too, 0 Mario only.\n");
    fprintf(f, "reflections = %d\n", s_reflections);
    fprintf(f, "# surface: 1 puddles reflect more or less by the floor under them, 0 all alike.\n");
    fprintf(f, "surface = %d\n", s_surface);
    fprintf(f, "# distance: how far from Mario reflections are drawn: 500, 750, 1000 (Original), 2000, 3000 or 5000.\n");
    fprintf(f, "distance = %d\n", kDistances[s_distance]);
    fprintf(f, "# ripples: the reflection in water wobbles: 0 off, 1 gentle, 2 strong.\n");
    fprintf(f, "ripples = %d\n", s_ripples);
    fprintf(f, "# What is reflected (the menu's Other page): 1 on, 0 off.\n");
    for (int i = 0; i < PORT_REFLECT_KINDS; i++) fprintf(f, "%s = %d\n", kKinds[i].key, s_show[i]);
    fprintf(f, "# waterreflections: 1 the stage's water and wet ground reflect as puddles do, 0 not.\n");
    fprintf(f, "waterreflections = %d\n", s_water);
    fprintf(f, "# How strongly puddles reflect on each surface, in percent (the menu's Other page).\n");
    for (int i = 0; i < PORT_PUDDLE_SURFACES; i++)
        fprintf(f, "%s = %d.%d\n", kSurfaces[i].key, s_strength[i] / 10, s_strength[i] % 10);
    fclose(f);
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
const char* distanceName(int i) {
    return i >= 0 && i < kDistanceCount ? kDistanceNames[i] : kDistanceNames[kDistanceOriginal];
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
    {nullptr, PORT_SETTINGS_PAGE_OTHER, ORDER, kKinds[N].name, kKinds[N].hint, 0, nullptr, offOnCount, offOnName, \
     showGet<N>, showSet<N>}
#define SURFACE_ROW(N, ORDER)                                                                           \
    {nullptr, PORT_SETTINGS_PAGE_OTHER, ORDER, kSurfaces[N].name, kSurfaces[N].hint, 0, nullptr, strengthCount, \
     strengthName, strengthGet<N>, strengthSet<N>}

// Name and hint: plain letters, digits, spaces and . , - ( ) ' only (port_settings.h).
const PortSetting kPuddleSettings[] = {
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 95, "Puddle Lifetime",
     "How long FLUDD's puddles last. Goop and objects react as in the original game.", 0, nullptr, count,
     name, get, set},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 96, "Puddle Reflections",
     "Characters and objects in FLUDD's puddles too, from the next area.", 1, nullptr, reflectionsCount,
     reflectionsName, reflectionsGet, reflectionsSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 97, "Reflections by Surface",
     "Puddles reflect more or less by surface. Set each on the Other page.", 0, nullptr, offOnCount, offOnName,
     surfaceGet, surfaceSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 98, "Reflection Distance",
     "How far from Mario puddles show reflections, fading out at the edge.", 0, nullptr, distanceCount,
     distanceName, distanceGet, distanceSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 99, "Water Reflections",
     "The sea, pools, fountains, streams and wet ground reflect as puddles do.", 0, nullptr, offOnCount,
     offOnName, waterGet, waterSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 100, "Water Ripples",
     "The reflection in water wobbles with moving waves. Puddles stay still.", 0, nullptr, ripplesCount,
     ripplesName, ripplesGet, ripplesSet},
    SHOW_ROW(PORT_REFLECT_MARIO, 1),
    SHOW_ROW(PORT_REFLECT_CHARACTERS, 2),
    SHOW_ROW(PORT_REFLECT_ENEMIES, 3),
    SHOW_ROW(PORT_REFLECT_ITEMS, 4),
    SHOW_ROW(PORT_REFLECT_OBJECTS, 5),
    SURFACE_ROW(PORT_PUDDLE_STONE, 10),
    SURFACE_ROW(PORT_PUDDLE_METAL, 20),
    SURFACE_ROW(PORT_PUDDLE_CLAY, 30),
    SURFACE_ROW(PORT_PUDDLE_WOOD, 40),
    SURFACE_ROW(PORT_PUDDLE_SAND, 50),
    SURFACE_ROW(PORT_PUDDLE_GRASS, 60),
    SURFACE_ROW(PORT_PUDDLE_CANVAS, 70),
    SURFACE_ROW(PORT_PUDDLE_WET, 80),
    SURFACE_ROW(PORT_PUDDLE_WATER, 90),
    SURFACE_ROW(PORT_PUDDLE_WET_GROUND, 100),
};

}  // namespace

extern "C" {

const PortSetting* port_puddle_settings(int* n) {
    *n = int(sizeof kPuddleSettings / sizeof kPuddleSettings[0]);
    return kPuddleSettings;
}

int port_puddle_life_scale(void) {
    load();
    return kScales[s_index];
}

int port_puddle_reflections(void) {
    load();
    return s_reflections;
}

int port_puddle_surface(void) {
    load();
    return s_surface;
}

int port_puddle_surface_strength(int surface) {
    load();
    return surface >= 0 && surface < PORT_PUDDLE_SURFACES ? s_strength[surface] : 1000;
}

int port_water_ripples(void) {
    load();
    return s_ripples;
}

int port_reflect_kind(int kind) {
    load();
    return kind >= 0 && kind < PORT_REFLECT_KINDS ? s_show[kind] : 1;
}

int port_water_reflections(void) {
    load();
    return s_water;
}

float port_puddle_reflection_distance(void) {
    load();
    return (float)kDistances[s_distance];
}

}  // extern "C"
