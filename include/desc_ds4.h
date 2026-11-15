#ifndef DESC_DS4_H
#define DESC_DS4_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#define USBD_VID_DS4          0x054C
#define USBD_PID_DS4          0x05C4
#define USBD_MANUFACTURER_DS4 "Sony Interactive Entertainment"
#define USBD_PRODUCT_DS4      "Wireless Controller"

// Sony DualShock 4 USB Input Report Payload (63 Bytes total, Report ID 0x01 is prepended by TinyUSB)
typedef struct __attribute__((packed)) {
    uint8_t  x;            // Left Stick X (0 .. 255, center 128)
    uint8_t  y;            // Left Stick Y (0 .. 255, center 128)
    uint8_t  z;            // Right Stick X (0 .. 255, center 128)
    uint8_t  rz;           // Right Stick Y (0 .. 255, center 128)
    uint8_t  buttons_dpad; // Lower 4 bits: D-Pad (0..7, 8=neutral), Upper 4 bits: Square, Cross, Circle, Triangle
    uint8_t  buttons_misc; // L1, R1, L2, R2, Share, Options, L3, R3
    uint8_t  buttons_sys;  // Bit 0: PS Button, Bit 1: Touchpad Click, Bits 2..7: Counter
    uint8_t  l2_trigger;   // Analog L2 Trigger (0 .. 255)
    uint8_t  r2_trigger;   // Analog R2 Trigger (0 .. 255)
    uint8_t  padding[54];  // Touchpad, motion sensors, battery status (zero-initialized)
} GamepadReportDS4_t;

// Sony DualShock 4 Output Report Structure (Report ID 0x05, 32 Bytes total)
typedef struct __attribute__((packed)) {
    uint8_t  report_id;    // 0x05
    uint8_t  flags;        // Bit 0: Rumble enable, Bit 1: LED enable
    uint8_t  reserved;
    uint8_t  motor_right;  // Light / high-frequency rumble motor (0 .. 255)
    uint8_t  motor_left;   // Heavy / low-frequency rumble motor (0 .. 255)
    uint8_t  led_r;        // Lightbar Red (0 .. 255)
    uint8_t  led_g;        // Lightbar Green (0 .. 255)
    uint8_t  led_b;        // Lightbar Blue (0 .. 255)
    uint8_t  extra[24];
} DS4OutputReport_t;

// Canonical Sony DualShock 4 USB HID Report Descriptor
uint8_t const desc_hid_report_ds4[] = {
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,        // USAGE (Game Pad)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x01,        //   REPORT_ID (1)
    0x09, 0x30,        //   USAGE (X) - Left Stick X
    0x09, 0x31,        //   USAGE (Y) - Left Stick Y
    0x09, 0x32,        //   USAGE (Z) - Right Stick X
    0x09, 0x35,        //   USAGE (Rz) - Right Stick Y
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00,  //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x04,        //   REPORT_COUNT (4)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x09, 0x39,        //   USAGE (Hat switch)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x07,        //   LOGICAL_MAXIMUM (7)
    0x35, 0x00,        //   PHYSICAL_MINIMUM (0)
    0x46, 0x3B, 0x01,  //   PHYSICAL_MAXIMUM (315)
    0x65, 0x14,        //   UNIT (Eng Rot:deg)
    0x75, 0x04,        //   REPORT_SIZE (4)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x42,        //   INPUT (Data,Var,Abs,Null)
    0x65, 0x00,        //   UNIT (None)
    0x05, 0x09,        //   USAGE_PAGE (Button)
    0x19, 0x01,        //   USAGE_MINIMUM (Button 1)
    0x29, 0x0E,        //   USAGE_MAXIMUM (Button 14)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,        //   REPORT_SIZE (1)
    0x95, 0x0E,        //   REPORT_COUNT (14)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x06, 0x00, 0xFF,  //   USAGE_PAGE (Vendor Specific 0xFF00)
    0x09, 0x20,        //   USAGE (0x20)
    0x75, 0x06,        //   REPORT_SIZE (6)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
    0x09, 0x33,        //   USAGE (Rx - Left Trigger L2)
    0x09, 0x34,        //   USAGE (Ry - Right Trigger R2)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00,  //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x02,        //   REPORT_COUNT (2)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x06, 0x00, 0xFF,  //   USAGE_PAGE (Vendor Specific 0xFF00)
    0x09, 0x21,        //   USAGE (0x21)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x36,        //   REPORT_COUNT (54)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x85, 0x05,        //   REPORT_ID (5) - Output Report for Rumble & LED
    0x09, 0x22,        //   USAGE (0x22)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x1F,        //   REPORT_COUNT (31)
    0x91, 0x02,        //   OUTPUT (Data,Var,Abs)

    // Feature Report 0x12 (Pairing / MAC Info - 15 bytes payload)
    0x06, 0x00, 0xFF,  //   USAGE_PAGE (Vendor Specific)
    0x09, 0x12,        //   USAGE (0x12)
    0x85, 0x12,        //   REPORT_ID (0x12)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x0F,        //   REPORT_COUNT (15)
    0xB1, 0x02,        //   FEATURE (Data,Var,Abs)

    // Feature Report 0x02 (Calibration Data - 36 bytes payload)
    0x09, 0x02,        //   USAGE (0x02)
    0x85, 0x02,        //   REPORT_ID (0x02)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x24,        //   REPORT_COUNT (36)
    0xB1, 0x02,        //   FEATURE (Data,Var,Abs)

    // Feature Report 0xA3 (Firmware Version Info - 48 bytes payload)
    0x09, 0xA3,        //   USAGE (0xA3)
    0x85, 0xA3,        //   REPORT_ID (0xA3)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x95, 0x30,        //   REPORT_COUNT (48)
    0xB1, 0x02,        //   FEATURE (Data,Var,Abs)
    0xC0               // END_COLLECTION
};

#endif // DESC_DS4_H

