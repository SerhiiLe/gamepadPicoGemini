#ifndef GAMEPAD_HID_H
#define GAMEPAD_HID_H

#include <Arduino.h>
#include "gamepad_modes.h"
#include "desc_pc.h"
#include "desc_android.h"
#include "desc_ds4.h"

// Feedback received from Host
extern volatile uint8_t g_rumble_left;      // Heavy motor (0..255) -> Red LED & GP18 PWM
extern volatile uint8_t g_rumble_right;     // Soft motor (0..255)  -> Blue LED
extern volatile uint8_t g_host_led_r;       // Host-requested LED Red (DS4)
extern volatile uint8_t g_host_led_g;       // Host-requested LED Green (DS4)
extern volatile uint8_t g_host_led_b;       // Host-requested LED Blue (DS4)
extern volatile bool    g_host_led_active;  // True if host is commanding LED

void gamepad_hid_init(GamepadMode mode);
GamepadMode gamepad_hid_get_mode();
bool gamepad_hid_send_report(uint32_t button_mask, int16_t lx, int16_t ly, int16_t rx, int16_t ry);

#endif // GAMEPAD_HID_H
