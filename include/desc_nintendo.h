#ifndef DESC_NINTENDO_H
#define DESC_NINTENDO_H

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// HORI Pokken Tournament DX Pro Pad for Nintendo Switch
#define USBD_VID_NINTENDO           0x0F0D
#define USBD_PID_NINTENDO           0x0092
#define USBD_MANUFACTURER_NINTENDO  "HORI CO.,LTD."
#define USBD_PRODUCT_NINTENDO       "POKKEN CONTROLLER"

// Nintendo Switch 8-Byte Input Report
typedef struct __attribute__((packed)) {
    uint16_t buttons;   // 16 bits: 13 buttons + 3 bits padding
    uint8_t  hat;       // 8-direction Hat switch (0..7, 8=Neutral)
    uint8_t  lx;        // Left Stick X (0..255, center 128)
    uint8_t  ly;        // Left Stick Y (0..255, center 128)
    uint8_t  rx;        // Right Stick X (0..255, center 128)
    uint8_t  ry;        // Right Stick Y (0..255, center 128)
    uint8_t  vendor;    // Vendor / Status byte (0x00)
} GamepadReportNintendo_t;

// Nintendo Switch HID Report Descriptor (HORI Pokken Protocol, 86 bytes)
uint8_t const desc_hid_report_nintendo[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0D,        //   Report Count (13 buttons)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0D,        //   Usage Maximum (Button 13)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x95, 0x03,        //   Report Count (3 padding bits)
    0x81, 0x01,        //   Input (Const, Array, Abs)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1 hat switch / D-Pad)
    0x65, 0x14,        //   Unit (Eng Rot: Angular Pos)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data, Var, Abs, Null State)
    0x65, 0x00,        //   Unit (None)
    0x95, 0x01,        //   Report Count (1 padding nibble)
    0x81, 0x01,        //   Input (Const, Array, Abs)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X - Left Stick X)
    0x09, 0x31,        //   Usage (Y - Left Stick Y)
    0x09, 0x32,        //   Usage (Z - Right Stick X)
    0x09, 0x33,        //   Usage (Rx - Right Stick Y)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4 axes)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,        //   Usage (Vendor Usage 0x20)
    0x95, 0x01,        //   Report Count (1 vendor byte)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x0A, 0x21, 0x26,  //   Usage (Vendor Usage 0x2621)
    0x95, 0x08,        //   Report Count (8 output bytes)
    0x91, 0x02,        //   Output (Data, Var, Abs)
    0xC0               // End Collection
};

#endif // DESC_NINTENDO_H
