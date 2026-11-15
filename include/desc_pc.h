#ifndef DESC_PC_H
#define DESC_PC_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#define USBD_VID_PC          0x2E8A
#define USBD_PID_PC          0x0013
#define USBD_PRODUCT_PC      "Gamepad rp2040"

typedef struct __attribute__((packed)) {
    uint8_t  buttons[2];   // 15 Digital Buttons + 1-bit padding (2 bytes)
    uint8_t  hat;          // 8-Way Hat Switch / D-Pad (1 byte: 0=Neutral, 1=Up..8=Up-Left)
    int16_t  x;            // Left Stick X (-32767 .. 32767)
    int16_t  y;            // Left Stick Y (-32767 .. 32767)
    int16_t  rx;           // Right Stick X (-32767 .. 32767)
    int16_t  ry;           // Right Stick Y (-32767 .. 32767)
    uint8_t  z;            // Left Trigger LT (0 .. 255)
    uint8_t  rz;           // Right Trigger RT (0 .. 255)
} GamepadReportPC_t;

// PC / Linux Desktop / Steam HID Report Descriptor
uint8_t const desc_hid_report_pc[] = {
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
    HID_USAGE(HID_USAGE_DESKTOP_Z),            // Left Trigger LT
    HID_USAGE(HID_USAGE_DESKTOP_RZ),           // Right Trigger RT
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
        HID_REPORT_COUNT(2),                   // Byte 0: Rumble, Byte 1: LED
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    HID_COLLECTION_END
};

#endif // DESC_PC_H

