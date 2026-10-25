// The port's puddle and water effects (port_puddles.h): how a mod sets them, through the mod API's
// api->reflections (ext/modapi/host/mod_runtime.cpp, SMS_REFLECT_* in the SDK's sms_mod.h). Each
// option is a number in the API's own units; each starts as the original game has it and goes back
// so when the mod that set it unloads (port_reflect_option_reset). The game's water, the mirror and
// the port's reflection code read them through the accessors at the end.
#include "port_puddles.h"

#include <stdio.h>

extern "C" void port_log(const char* fmt, ...);

namespace {

struct Option {
    const char* name;  // for the log
    int min, max, original;
};

// In SMS_REFLECT_*'s order (the SDK's sms_mod.h): never reorder, only add at the end.
const Option kOptions[PORT_REFLECT_OPTIONS] = {
    {"Puddle Lifetime", 1, 8, 1},
    {"Puddle Reflections", 0, 1, 0},
    {"Reflection Distance", 500, 5000, 1000},
    {"Water Reflections", 0, 1, 0},
    {"Puddles Beat Mirrors", 0, 1, 0},
    {"Reflect the Level", 0, 1, 0},
    {"Viewing Angle", 0, 2, 0},
    {"Puddle Ripples", 0, 2, 0},
    {"Water Ripples", 0, 2, 0},
    {"Sun Glints", 0, 2, 0},
    {"Water Depth", 0, 2, 0},
    {"Reflection Blur", 0, 1000, 0},
    {"Wet Footprints", 0, 1, 0},
    {"Reflect Mario and Yoshi", 0, 1, 1},
    {"Reflect Characters", 0, 1, 0},
    {"Reflect Enemies", 0, 1, 0},
    {"Reflect Items", 0, 1, 0},
    {"Reflect Objects", 0, 1, 0},
    {"Reflections by Surface", 0, 1, 0},
    {"Stone and Carpets", 0, 1000, 300},
    {"Metal", 0, 1000, 500},
    {"Clay Roof Tiles", 0, 1000, 400},
    {"Wood", 0, 1000, 330},
    {"Sand", 0, 1000, 50},
    {"Grass", 0, 1000, 60},
    {"Canvas", 0, 1000, 100},
    {"Wet Floors", 0, 1000, 550},
    {"Water", 0, 1000, 500},
    {"Wet Ground", 0, 1000, 450},
};

int s_value[PORT_REFLECT_OPTIONS];
bool s_ready;

void ready() {
    if (s_ready) return;
    s_ready = true;
    for (int i = 0; i < PORT_REFLECT_OPTIONS; i++) s_value[i] = kOptions[i].original;
}

int value(int option) {
    ready();
    return s_value[option];
}

}  // namespace

extern "C" {

int port_reflect_option_set(int option, int v) {
    if (option < 0 || option >= PORT_REFLECT_OPTIONS) return 0;
    ready();
    const Option& o = kOptions[option];
    v = v < o.min ? o.min : v > o.max ? o.max : v;
    if (s_value[option] != v) {
        s_value[option] = v;
        port_log("[puddles] %s: %d\n", o.name, v);
    }
    return 1;
}

int port_reflect_option_get(int option) {
    return option >= 0 && option < PORT_REFLECT_OPTIONS ? value(option) : -1;
}

int port_reflect_option_range(int option, int* min, int* max, int* original) {
    if (option < 0 || option >= PORT_REFLECT_OPTIONS) return 0;
    if (min) *min = kOptions[option].min;
    if (max) *max = kOptions[option].max;
    if (original) *original = kOptions[option].original;
    return 1;
}

void port_reflect_option_reset(void) {
    s_ready = false;
    ready();
    port_log("[puddles] every option as the original game has it\n");
}

int port_puddle_life_scale(void) { return value(PORT_OPT_LIFETIME); }
int port_puddle_reflections(void) { return value(PORT_OPT_PUDDLES); }
float port_puddle_reflection_distance(void) { return (float)value(PORT_OPT_DISTANCE); }
int port_water_reflections(void) { return value(PORT_OPT_WATER); }
int port_puddles_beat_mirrors(void) { return value(PORT_OPT_BEAT_MIRRORS); }
int port_reflect_level(void) { return value(PORT_OPT_LEVEL); }
int port_reflection_view_angle(void) { return value(PORT_OPT_VIEW_ANGLE); }
int port_puddle_ripples(void) { return value(PORT_OPT_PUDDLE_RIPPLES); }
int port_water_ripples(void) { return value(PORT_OPT_WATER_RIPPLES); }
int port_sun_glints(void) { return value(PORT_OPT_SUN_GLINTS); }
int port_water_depth(void) { return value(PORT_OPT_WATER_DEPTH); }
int port_reflection_blur(void) { return value(PORT_OPT_BLUR); }
int port_wet_footprints(void) { return value(PORT_OPT_FOOTPRINTS); }
int port_reflect_kind(int kind) {
    return kind >= 0 && kind < PORT_REFLECT_KINDS ? value(PORT_OPT_SHOW + kind) : 1;
}
int port_puddle_surface(void) { return value(PORT_OPT_BY_SURFACE); }
int port_puddle_surface_strength(int surface) {
    return surface >= 0 && surface < PORT_PUDDLE_SURFACES ? value(PORT_OPT_STRENGTH + surface) : 1000;
}

}  // extern "C"
