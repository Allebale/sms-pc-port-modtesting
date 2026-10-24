// FLUDD's puddles and the stage's water (port_puddles.h): Puddle Lifetime on the settings menu's
// Graphics page, the rest on its Reflections page, in the order kPuddleSettings gives. Puddle Lifetime: how many
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
int s_puddleRipples = 1; // Puddle Ripples: 0 off, 1 gentle, 2 strong
int s_beatMirrors = 1;    // Puddles Beat Mirrors
int s_level = 0;          // Reflect the Level: 0 off (as the game has it), 1 the stage itself too
int s_water;       // Water Reflections: 0 none (as the game has it), 1 in the stage's water and wet ground
const int kDistances[] = {500, 750, 1000, 2000, 3000, 5000};
const char* const kDistanceNames[] = {"0.5x", "0.75x", "Original", "2x", "3x", "5x"};
const int kDistanceCount = int(sizeof kDistances / sizeof kDistances[0]);
const int kDistanceOriginal = 2;      // 1000, the game's
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
int s_blur = 2;        // Reflection Blur: 0 sharp to 10
bool s_strengthSet;
int s_viewAngle = 1;  // Viewing Angle: 0 off, 1 subtle, 2 realistic
int s_glints = 1;     // Sun Glints: 0 off, 1 subtle, 2 bright
int s_depth = 1;      // Water Depth: 0 off, 1 subtle, 2 strong

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
        } else if (sscanf(line, " blur = %d", &value) == 1) {
            s_blur = value < 0 ? 0 : value > 10 ? 10 : value;
        } else if (sscanf(line, " viewangle = %d", &value) == 1) {
            s_viewAngle = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " glints = %d", &value) == 1) {
            s_glints = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (sscanf(line, " depth = %d", &value) == 1) {
            s_depth = value < 0 ? 0 : value > 2 ? 2 : value;
        } else if (isKind(line, &value)) {
            // done by isKind
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
    fprintf(f, "# puddleripples: rings spread in puddles where drops land and Mario runs: 0 off, 1 gentle, 2 strong.\n");
    fprintf(f, "puddleripples = %d\n", s_puddleRipples);
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
    fprintf(f, "# blur: how blurred reflections in puddles and water are, 0 sharp to 10.\n");
    fprintf(f, "blur = %d\n", s_blur);
    fprintf(f, "# What is reflected (the menu's Reflections page): 1 on, 0 off.\n");
    for (int i = 0; i < PORT_REFLECT_KINDS; i++) fprintf(f, "%s = %d\n", kKinds[i].key, s_show[i]);
    fprintf(f, "# waterreflections: 1 the stage's water and wet ground reflect as puddles do, 0 not.\n");
    fprintf(f, "waterreflections = %d\n", s_water);
    fprintf(f, "# How strongly puddles reflect on each surface, in percent (the menu's Reflections page).\n");
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

int blurCount() { return 11; }
const char* blurName(int i) {
    static char text[8];
    snprintf(text, sizeof text, "%d", i < 0 ? 0 : i > 10 ? 10 : i);
    return text;
}
int blurGet() {
    load();
    return s_blur;
}
int blurSet(int i) {
    if (i < 0 || i > 10) return 0;
    load();
    s_blur = i;
    save();
    port_log("[puddles] Reflection Blur: %d\n", i);
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

int port_puddle_ripples(void) {
    load();
    return s_puddleRipples;
}

int port_water_ripples(void) {
    load();
    return s_ripples;
}

int port_reflect_kind(int kind) {
    load();
    return kind >= 0 && kind < PORT_REFLECT_KINDS ? s_show[kind] : 1;
}

int port_reflection_view_angle(void) {
    load();
    return s_viewAngle;
}

int port_reflection_blur(void) {
    load();
    return s_blur;
}

int port_sun_glints(void) {
    load();
    return s_glints;
}

int port_water_depth(void) {
    load();
    return s_depth;
}

int port_reflect_level(void) {
    load();
    return s_level;
}

int port_puddles_beat_mirrors(void) {
    load();
    return s_beatMirrors;
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
