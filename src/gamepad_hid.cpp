#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "config.h"
#include "gamepad_hid.h"

volatile uint8_t g_rumble_intensity = 0;
volatile uint8_t g_led_intensity = 0;

// Standard USB HID Gamepad Descriptor (15 Digital Buttons, 8-way Hat Switch, 4 16-bit Axes, 2 8-bit Triggers, PID Output Report)
uint8_t const desc_hid_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),

    // --- Input Report: 15 Digital Buttons (15 bits + 1 bit padding = 2 Bytes) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
    HID_USAGE_MIN(1),
    HID_USAGE_MAX(15),
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(15),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    // 1-bit constant padding to complete 2 bytes
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_CONSTANT),

    // --- Input Report: Hat Switch / D-Pad (1 Byte) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_HAT_SWITCH),
    HID_LOGICAL_MIN(1),
    HID_LOGICAL_MAX(8),
    HID_PHYSICAL_MIN(0),
    HID_PHYSICAL_MAX_N(315, 2),
    HID_UNIT(0x14), // English Rotation: deg
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NULL_STATE),

    // --- Input Report: 4 Analog Stick Axes (LX, LY, RX, RY) (8 Bytes: -32767 .. 32767) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_POINTER),
    HID_COLLECTION(HID_COLLECTION_PHYSICAL),
        HID_USAGE(HID_USAGE_DESKTOP_X),        // Left Stick X
        HID_USAGE(HID_USAGE_DESKTOP_Y),        // Left Stick Y
        HID_USAGE(HID_USAGE_DESKTOP_RX),       // Right Stick X
        HID_USAGE(HID_USAGE_DESKTOP_RY),       // Right Stick Y
        HID_LOGICAL_MIN_N(-32767, 2),
        HID_LOGICAL_MAX_N(32767, 2),
        HID_REPORT_SIZE(16),
        HID_REPORT_COUNT(4),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    // --- Input Report: 2 Analog Trigger Axes (LT, RT) (2 Bytes: 0 .. 255) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_Z),            // Left Trigger LT (Brake)
    HID_USAGE(HID_USAGE_DESKTOP_RZ),           // Right Trigger RT (Accelerator)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX_N(255, 2),                 // Positive 255 (2 bytes)
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(2),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    // --- Output Report: Rumble & LED Feedback from Host (2 Bytes) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID),
    HID_USAGE(0x21),                           // Set Effect Report
    HID_COLLECTION(HID_COLLECTION_LOGICAL),
        HID_USAGE(0x97),                       // DC Enable Actuators
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX_N(255, 2),             // 2-byte max to represent positive 255
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(2),                   // Byte 0: Rumble, Byte 1: LED
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    HID_COLLECTION_END                         // End Application Collection
};

static Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE, 1, true);

static void hid_set_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) report_id;
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
        g_rumble_intensity = buffer[0];
        if (bufsize >= 2) {
            g_led_intensity = buffer[1];
        }
    }
}

// Compute 8-Way Hat Switch direction from D-Pad buttons (Bits 13..16 in button_mask)
static uint8_t compute_hat_switch(uint32_t button_mask) {
    bool up    = (button_mask & (1UL << 13)) != 0;
    bool down  = (button_mask & (1UL << 14)) != 0;
    bool left  = (button_mask & (1UL << 15)) != 0;
    bool right = (button_mask & (1UL << 16)) != 0;

    // SOCD Cleaning: Opposing cardinal directions cancel out
    if (up && down) { up = false; down = false; }
    if (left && right) { left = false; right = false; }

    if (up && right)   return 2; // 45 deg (Up-Right)
    if (down && right) return 4; // 135 deg (Down-Right)
    if (down && left)  return 6; // 225 deg (Down-Left)
    if (up && left)    return 8; // 315 deg (Up-Left)
    if (up)            return 1; // 0 deg (Up)
    if (right)         return 3; // 90 deg (Right)
    if (down)          return 5; // 180 deg (Down)
    if (left)          return 7; // 270 deg (Left)

    return 0; // 0 = Null State (Neutral / Released)
}

void gamepad_hid_init() {
    // Detach from USB bus to force host OS re-enumeration of updated device descriptors
    if (TinyUSBDevice.mounted() || TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.detach();
        delay(100);
    }

    // Set custom USB Vendor ID, Product ID, Manufacturer and Product strings
    TinyUSBDevice.setID(GAMEPAD_USBD_VID, GAMEPAD_USBD_PID);
    TinyUSBDevice.setManufacturerDescriptor(GAMEPAD_USBD_MANUFACTURER);
    TinyUSBDevice.setProductDescriptor(GAMEPAD_USBD_PRODUCT);

    usb_hid.setPollInterval(4); // 4ms polling rate
    usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
    usb_hid.setReportCallback(NULL, hid_set_report_cb);
    usb_hid.begin();

    // Re-attach to USB bus with updated descriptors
    TinyUSBDevice.attach();
}

bool gamepad_hid_send_report(uint32_t button_mask, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    if (!usb_hid.ready()) {
        return false;
    }

    GamepadInputReport_t report;
    memset(&report, 0, sizeof(report));
    
    // Map Digital Buttons according to Google / Android Gamepad Specification:
    // Button 1: A, Button 2: B, Button 3: C (0), Button 4: X, Button 5: Y, Button 6: Z (0)
    // Button 7: LB, Button 8: RB, Button 9: LT, Button 10: RT, Button 11: Select, Button 12: Start,
    // Button 13: Central (Mode), Button 14: L3, Button 15: R3, Button 16: Padding (0)
    
    // Byte 0 (report.buttons[0]): Buttons 1..8
    report.buttons[0] = 0;
    if (button_mask & (1UL << 0))  report.buttons[0] |= (1 << 0); // Button 1: A
    if (button_mask & (1UL << 1))  report.buttons[0] |= (1 << 1); // Button 2: B
    // Bit 2 is 0: Button 3 (C gap for Sega compatibility)
    if (button_mask & (1UL << 2))  report.buttons[0] |= (1 << 3); // Button 4: X
    if (button_mask & (1UL << 3))  report.buttons[0] |= (1 << 4); // Button 5: Y
    // Bit 5 is 0: Button 6 (Z gap for Sega compatibility)
    if (button_mask & (1UL << 4))  report.buttons[0] |= (1 << 6); // Button 7: LB
    if (button_mask & (1UL << 5))  report.buttons[0] |= (1 << 7); // Button 8: RB

    // Byte 1 (report.buttons[1]): Buttons 9..16
    report.buttons[1] = 0;
    if (button_mask & (1UL << 6))  report.buttons[1] |= (1 << 0); // Button 9: LT (Digital button duplication)
    if (button_mask & (1UL << 7))  report.buttons[1] |= (1 << 1); // Button 10: RT (Digital button duplication)
    if (button_mask & (1UL << 8))  report.buttons[1] |= (1 << 2); // Button 11: Select
    if (button_mask & (1UL << 9))  report.buttons[1] |= (1 << 3); // Button 12: Start
    if (button_mask & (1UL << 12)) report.buttons[1] |= (1 << 4); // Button 13: Central (Mode / Home)
    if (button_mask & (1UL << 10)) report.buttons[1] |= (1 << 5); // Button 14: L3 (Left Stick click)
    if (button_mask & (1UL << 11)) report.buttons[1] |= (1 << 6); // Button 15: R3 (Right Stick click)
    // Bit 7 is 0: Button 16 (Padding)

    // Compute Hat Switch (D-Pad) position from bits 13..16
    report.hat = compute_hat_switch(button_mask);

    // Analog joysticks
    report.x  = lx;
    report.y  = ly;
    report.rx = rx;
    report.ry = ry;

    // Synthesize analog trigger axes Z and Rz with smooth ramping (0 .. 255)
    static float s_lt_axis = 0.0f;
    static float s_rt_axis = 0.0f;
    static unsigned long s_last_trigger_time = 0;

    unsigned long now = millis();
    unsigned long dt = (s_last_trigger_time == 0) ? 1 : (now - s_last_trigger_time);
    s_last_trigger_time = now;
    if (dt > 50) dt = 50;

    float step = (255.0f / (float)TRIGGER_RAMP_MS) * (float)dt;

    bool lt_pressed = (button_mask & (1UL << 6)) != 0;
    bool rt_pressed = (button_mask & (1UL << 7)) != 0;

    if (lt_pressed) {
        s_lt_axis += step;
        if (s_lt_axis > 255.0f) s_lt_axis = 255.0f;
    } else {
        s_lt_axis -= step;
        if (s_lt_axis < 0.0f) s_lt_axis = 0.0f;
    }

    if (rt_pressed) {
        s_rt_axis += step;
        if (s_rt_axis > 255.0f) s_rt_axis = 255.0f;
    } else {
        s_rt_axis -= step;
        if (s_rt_axis < 0.0f) s_rt_axis = 0.0f;
    }

    report.z  = (uint8_t)(s_lt_axis + 0.5f);
    report.rz = (uint8_t)(s_rt_axis + 0.5f);

    return usb_hid.sendReport(0, &report, sizeof(report));
}
