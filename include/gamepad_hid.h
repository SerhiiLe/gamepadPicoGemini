#ifndef GAMEPAD_HID_H
#define GAMEPAD_HID_H

typedef struct __attribute__((packed)) {
    uint8_t  buttons[2];   // 16 Digital Buttons (2 bytes / 16 bits total)
    uint8_t  hat;          // 8-Way Hat Switch / D-Pad (1 byte: 0=Neutral, 1=Up, 2=Up-Right, ..., 8=Up-Left)
    int16_t  x;            // Left Stick X (-32767 .. 32767)
    int16_t  y;            // Left Stick Y (-32767 .. 32767)
    int16_t  rx;           // Right Stick X (-32767 .. 32767)
    int16_t  ry;           // Right Stick Y (-32767 .. 32767)
    int16_t  z;            // Left Trigger LT (-32767 .. 32767)
    int16_t  rz;           // Right Trigger RT (-32767 .. 32767)
} GamepadInputReport_t;

// Feedback received from Host
extern volatile uint8_t g_rumble_intensity; // 0..255
extern volatile uint8_t g_led_intensity;    // 0..255

void gamepad_hid_init();
bool gamepad_hid_send_report(uint32_t button_mask, int16_t lx, int16_t ly, int16_t rx, int16_t ry);

#endif // GAMEPAD_HID_H
