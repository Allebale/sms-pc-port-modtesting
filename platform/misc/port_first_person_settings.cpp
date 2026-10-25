// The settings menu's First Person page (port_settings.h, PORT_SETTINGS_PAGE_FIRST_PERSON): the
// built-in first-person camera (ext/core/game/port_first_person.cpp) and how it plays. The
// FIRST_PERSON key (V by default, platform/pad/pad.cpp) and the controller button chosen here
// switch it on and off while playing.
//
// The launcher has no words for these, so they are kept in a file of their own, firstperson.txt in
// the game's data folder (beside the memory card's, card-a). Their ids are null: the menu does not
// try to save them for the launcher.
#include "port_settings.h"
#include "port_first_person.h"

#include <dolphin/pad.h>
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

// Each setting: its key in the file, and its value as an index into its list
struct Value {
    const char* key;
    int index;
    int count;
    int def;
};
enum { ON, BUTTON, FOV, EYE, TURN, TILT, INVERT_X, INVERT_Y, MODEL, AIM, RUN, COUNT };
Value s_v[COUNT] = {
    {"on", 0, 2, 0},
    {"button", 0, 5, 0},            // D-pad Down
    {"fov", 4, 13, 4},              // 40 + 5 * i: 60
    {"eye_height", 14, 29, 14},     // 60 + 5 * i: 130
    {"turn_speed", 10, 31, 10},     // 30 + 15 * i: 180
    {"tilt_speed", 9, 25, 9},       // 10 * i: 90
    {"invert_x", 0, 2, 0},
    {"invert_y", 0, 2, 0},
    {"model", 1, 3, 1},             // Hidden
    {"aim_fludd", 1, 2, 1},
    {"run_spray", 1, 2, 1},
};
bool s_loaded;

// ------------------------------------------------------------------ the file
// <data>/sms-port/firstperson.txt, where <data> is as the memory card's: SMS_SAVE_DIR's parent when
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
    if (const char* d = getenv("SMS_SAVE_DIR")) {
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
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == L'/' || dir[i] == L'\\') _wmkdir(dir.substr(0, i).c_str());
    return _wfopen((dir + L"/firstperson.txt").c_str(), mode);
}
#define READ L"r"
#define WRITE L"w"
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
    return fopen((dir + "/firstperson.txt").c_str(), mode);
}
#define READ "r"
#define WRITE "w"
#endif

void load() {
    if (s_loaded) return;
    s_loaded = true;
    FILE* f = openFile(READ);
    if (!f) return;  // none yet: the defaults
    char line[128], key[32];
    int value;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, " %31[a-z_] = %d", key, &value) != 2) continue;
        for (int i = 0; i < COUNT; i++)
            if (!strcmp(key, s_v[i].key)) s_v[i].index = value < 0 ? 0 : value >= s_v[i].count ? s_v[i].count - 1 : value;
    }
    fclose(f);
}

void save() {
    FILE* f = openFile(WRITE);
    if (!f) {
        port_log("[first person] could not save its settings (firstperson.txt)\n");
        return;
    }
    fprintf(f, "# The first-person camera (the pause menu's Settings, First Person): each a position in its list.\n");
    for (int i = 0; i < COUNT; i++) fprintf(f, "%s = %d\n", s_v[i].key, s_v[i].index);
    fclose(f);
}

int value(int which) {
    load();
    return s_v[which].index;
}
int setValue(int which, int i) {
    if (i < 0 || i >= s_v[which].count) return 0;
    load();
    s_v[which].index = i;
    save();
    return 1;
}

const char* number(int n) {
    static char text[16];
    snprintf(text, sizeof text, "%d", n);
    return text;
}

int offOnCount() { return 2; }
const char* offOnName(int i) { return i ? "On" : "Off"; }

int onGet() { return value(ON); }
int onSet(int i) {
    if (!setValue(ON, i)) return 0;
    port_log("[first person] %s (the settings menu)\n", i ? "on" : "off");
    return 1;
}

const char* const kButtons[] = {"D-pad Down", "D-pad Up", "D-pad Left", "D-pad Right", "None"};
int buttonCount() { return 5; }
const char* buttonName(int i) { return kButtons[i < 0 || i > 4 ? 0 : i]; }
int buttonGet() { return value(BUTTON); }
int buttonSet(int i) { return setValue(BUTTON, i); }

int fovCount() { return 13; }
const char* fovName(int i) { return number(40 + 5 * i); }
int fovGet() { return value(FOV); }
int fovSet(int i) { return setValue(FOV, i); }

int eyeCount() { return 29; }
const char* eyeName(int i) { return number(60 + 5 * i); }
int eyeGet() { return value(EYE); }
int eyeSet(int i) { return setValue(EYE, i); }

int turnCount() { return 31; }
const char* turnName(int i) { return number(30 + 15 * i); }
int turnGet() { return value(TURN); }
int turnSet(int i) { return setValue(TURN, i); }

int tiltCount() { return 25; }
const char* tiltName(int i) { return number(10 * i); }
int tiltGet() { return value(TILT); }
int tiltSet(int i) { return setValue(TILT, i); }

int invertXGet() { return value(INVERT_X); }
int invertXSet(int i) { return setValue(INVERT_X, i); }
int invertYGet() { return value(INVERT_Y); }
int invertYSet(int i) { return setValue(INVERT_Y, i); }

const char* const kModels[] = {"Body Only", "Hidden", "Shown"};
int modelCount() { return 3; }
const char* modelName(int i) { return kModels[i < 0 || i > 2 ? 0 : i]; }
int modelGet() { return value(MODEL); }
int modelSet(int i) { return setValue(MODEL, i); }

int aimGet() { return value(AIM); }
int aimSet(int i) { return setValue(AIM, i); }
int runGet() { return value(RUN); }
int runSet(int i) { return setValue(RUN, i); }

#define ROW(ORDER, NAME, HINT, COUNTF, NAMEF, GETF, SETF) \
    {nullptr, PORT_SETTINGS_PAGE_FIRST_PERSON, ORDER, NAME, HINT, 0, nullptr, COUNTF, NAMEF, GETF, SETF}

// Name and hint: plain letters, digits, spaces and . , - ( ) ' only (port_settings.h).
const PortSetting kSettings[] = {
    ROW(1, "First Person", "See through Mario's eyes. The V key or the button below switches it.", offOnCount,
        offOnName, onGet, onSet),
    ROW(2, "Toggle Button", "The controller button that switches first person on and off.", buttonCount,
        buttonName, buttonGet, buttonSet),
    ROW(3, "Field of View", "How much is seen, in degrees up and down.", fovCount, fovName, fovGet, fovSet),
    ROW(4, "Eye Height", "How high above Mario's feet the view is. Higher on Yoshi by itself.", eyeCount, eyeName,
        eyeGet, eyeSet),
    ROW(5, "Turn Speed", "How fast the C-stick turns the view, in degrees a second.", turnCount, turnName, turnGet,
        turnSet),
    ROW(6, "Tilt Speed", "How fast the C-stick tilts the view up and down.", tiltCount, tiltName, tiltGet, tiltSet),
    ROW(7, "Invert Left and Right", "The C-stick turns the view the other way.", offOnCount, offOnName, invertXGet,
        invertXSet),
    ROW(8, "Invert Up and Down", "The C-stick tilts the view the other way.", offOnCount, offOnName, invertYGet,
        invertYSet),
    ROW(9, "Mario in First Person", "Body Only shows his body looking down, without his head or FLUDD.",
        modelCount, modelName, modelGet, modelSet),
    ROW(10, "FLUDD Aims Where You Look", "While R is held, FLUDD sprays where the view looks, up and down too.",
        offOnCount, offOnName, aimGet, aimSet),
    ROW(11, "Full R Keeps Moving", "R all the way down sprays at full strength without stopping Mario.",
        offOnCount, offOnName, runGet, runSet),
};

}  // namespace

extern "C" {

const PortSetting* port_first_person_settings(int* n) {
    *n = int(sizeof kSettings / sizeof kSettings[0]);
    return kSettings;
}

int port_fp_on(void) { return value(ON); }
void port_fp_toggle(void) {
    load();
    s_v[ON].index = !s_v[ON].index;
    save();
    port_log("[first person] %s\n", s_v[ON].index ? "on" : "off");
}
unsigned port_fp_button(void) {
    static const unsigned bits[] = {PAD_BUTTON_DOWN, PAD_BUTTON_UP, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT, 0};
    return bits[value(BUTTON)];
}
int port_fp_fov(void) { return 40 + 5 * value(FOV); }
int port_fp_eye_height(void) { return 60 + 5 * value(EYE); }
int port_fp_turn_speed(void) { return 30 + 15 * value(TURN); }
int port_fp_tilt_speed(void) { return 10 * value(TILT); }
int port_fp_invert_x(void) { return value(INVERT_X); }
int port_fp_invert_y(void) { return value(INVERT_Y); }
int port_fp_model(void) { return value(MODEL); }
int port_fp_aim_fludd(void) { return value(AIM); }
int port_fp_run_spray(void) { return value(RUN); }

}  // extern "C"
