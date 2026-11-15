#ifndef DESC_ANDROID_H
#define DESC_ANDROID_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#define USBD_VID_ANDROID          0x2E8A
#define USBD_PID_ANDROID          0x0016
#define USBD_PRODUCT_ANDROID      "Gamepad Android"

typedef struct __attribute__((packed)) {
    uint8_t  buttons[2];   // 15 Digital Buttons (with Sega C/Z gaps) + 1-bit padding (2 bytes)
    uint8_t  hat;          // 8-Way Hat Switch / D-Pad (1 byte: 0=Neutral, 1=Up..8=Up-Left)
    int16_t  x;            // Left Stick X (-32767 .. 32767)
    int16_t  y;            // Left Stick Y (-32767 .. 32767)
    int16_t  z;            // Right Stick X (Usage Z: -32767 .. 32767)
    int16_t  rz;           // Right Stick Y (Usage Rz: -32767 .. 32767)
    uint8_t  brake;        // Left Trigger LT (Simulation Brake: 0 .. 255)
    uint8_t  accelerator;  // Right Trigger RT (Simulation Accelerator: 0 .. 255)
} GamepadReportAndroid_t;

// Android / Google Spec Gamepad HID Report Descriptor
uint8_t const desc_hid_report_android[] = {
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
    // 1-bit padding
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
    // Google/Android spec maps Right Stick to Z (0x32) and Rz (0x35)
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_POINTER),
    HID_COLLECTION(HID_COLLECTION_PHYSICAL),
        HID_USAGE(HID_USAGE_DESKTOP_X),        // Left Stick X
        HID_USAGE(HID_USAGE_DESKTOP_Y),        // Left Stick Y
        HID_USAGE(HID_USAGE_DESKTOP_Z),        // Right Stick X (Usage Z -> AXIS_Z)
        HID_USAGE(HID_USAGE_DESKTOP_RZ),       // Right Stick Y (Usage Rz -> AXIS_RZ)
        HID_LOGICAL_MIN_N(-32767, 2),
        HID_LOGICAL_MAX_N(32767, 2),
        HID_REPORT_SIZE(16),
        HID_REPORT_COUNT(4),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    // --- Input Report: 2 Analog Trigger Axes (LT, RT) (2 Bytes: 0 .. 255) ---
    // Simulation Controls Page: Brake (0x4D) & Accelerator (0x43) -> AXIS_BRAKE & AXIS_GAS
    HID_USAGE_PAGE(0x02),                      // Simulation Controls Page
    HID_USAGE(0x4D),                           // Brake (LT)
    HID_USAGE(0x43),                           // Accelerator (RT)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX_N(255, 2),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(2),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    // --- Output Report: Rumble & LED Feedback from Host (2 Bytes) ---
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID),
    HID_USAGE(0x21),                           // Set Effect Report
    HID_COLLECTION(HID_COLLECTION_LOGICAL),
        HID_USAGE(0x97),                       // DC Enable Actuators
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX_N(255, 2),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(2),
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    HID_COLLECTION_END
};

#endif // DESC_ANDROID_H

