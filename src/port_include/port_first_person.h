#ifndef PORT_FIRST_PERSON_H
#define PORT_FIRST_PERSON_H

// The first-person camera, built in: its settings menu page (platform/misc/
// port_first_person_settings.cpp, kept in firstperson.txt) and the FIRST_PERSON key, read by the
// camera itself (ext/core/game/port_first_person.cpp). Plain C, for the game and the menu alike.

#ifdef __cplusplus
extern "C" {
#endif

int port_fp_on(void);          // first person now
void port_fp_toggle(void);     // the FIRST_PERSON key, or the controller's button
unsigned port_fp_button(void); // the controller's toggle button (PAD_BUTTON_*), 0 none
int port_fp_fov(void);         // degrees, up and down
int port_fp_eye_height(void);  // units above his feet
int port_fp_turn_speed(void);  // degrees a second, the C-stick all the way
int port_fp_tilt_speed(void);
int port_fp_invert_x(void);
int port_fp_invert_y(void);
// What of Mario shows in first person
enum { PORT_FP_MODEL_BODY = 0, PORT_FP_MODEL_HIDDEN = 1, PORT_FP_MODEL_SHOWN = 2 };
int port_fp_model(void);
int port_fp_aim_fludd(void);   // FLUDD sprays where the view looks
int port_fp_run_spray(void);   // R all the way down keeps him moving

#ifdef __cplusplus
}
#endif

#endif
