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
    port_log("[puddles] %s: lifetime %dx, reflections %s, by surface %s (%s)\n", why, kScales[s_index],
             s_reflections ? "everything" : "Mario only", s_surface ? "on" : "off", pathForLog().c_str());
}

void load() {
    if (s_loaded) return;
    s_loaded = true;
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

// Name and hint: plain letters, digits, spaces and . , - ( ) ' only (port_settings.h).
const PortSetting kPuddleSettings[] = {
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 95, "Puddle Lifetime",
     "How long FLUDD's puddles last. Goop and objects react as in the original game.", 0, nullptr, count,
     name, get, set},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 96, "Puddle Reflections",
     "Characters and objects in FLUDD's puddles too, from the next area.", 1, nullptr, reflectionsCount,
     reflectionsName, reflectionsGet, reflectionsSet},
    {nullptr, PORT_SETTINGS_PAGE_GRAPHICS, 97, "Reflections by Surface",
     "Puddles reflect less on sand and soft floors, fully on smooth ones.", 0, nullptr, offOnCount, offOnName,
     surfaceGet, surfaceSet},
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

}  // extern "C"
