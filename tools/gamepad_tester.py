#!/usr/bin/env python3
"""
Gamepad Tester for Linux (DualShock 4, Xbox 360, RP2040 HID Gamepads).
Supports testing rumble motors (Left/Right/Both) and DS4 RGB Lightbar.
Works via GUI (Tkinter) or interactive terminal menu (CLI).
"""

import os
import sys
import glob
import time
import fcntl
import struct
import argparse
import threading

# Linux Input Subsystem constants
EV_KEY = 0x01
EV_REL = 0x02
EV_ABS = 0x03
EV_FF  = 0x15

FF_RUMBLE = 0x50

# ioctl request codes for 64-bit Linux
# EVIOCGBIT(ev, len) = _IOC(_IOC_READ, 'E', 0x20 + ev, len)
EVIOCGBIT_EV  = 2 << 30 | 4 << 16 | ord('E') << 8 | 0x20
EVIOCGBIT_FF  = 2 << 30 | 16 << 16 | ord('E') << 8 | (0x20 + 0x15)
# EVIOCGNAME(len)
EVIOCGNAME    = 2 << 30 | 256 << 16 | ord('E') << 8 | 0x06
# EVIOCSFF = _IOW('E', 0x80, struct ff_effect) -> size 48 bytes
EVIOCSFF      = 1 << 30 | 48 << 16 | ord('E') << 8 | 0x80
# HIDIOCGRAWNAME(len)
HIDIOCGRAWNAME = 2 << 30 | 256 << 16 | ord('H') << 8 | 0x04


class GamepadDevice:
    def __init__(self, name, event_path=None, hidraw_path=None, is_ds4=False, is_xpad=False):
        self.name = name
        self.event_path = event_path
        self.hidraw_path = hidraw_path
        self.is_ds4 = is_ds4
        self.is_xpad = is_xpad
        self.active_effect_id = -1

    def send_rumble(self, strong_pct=0, weak_pct=0, duration_ms=1000):
        """
        strong_pct (0..100): Left / heavy motor
        weak_pct (0..100): Right / light motor
        """
        # If DS4 and hidraw is available, we can send raw packet
        if self.is_ds4 and self.hidraw_path:
            try:
                motor_l = int((strong_pct / 100.0) * 255)
                motor_r = int((weak_pct / 100.0) * 255)
                self.send_ds4_raw(motor_l=motor_l, motor_r=motor_r)
                if duration_ms > 0:
                    def _stop_after():
                        time.sleep(duration_ms / 1000.0)
                        self.send_ds4_raw(motor_l=0, motor_r=0)
                    threading.Thread(target=_stop_after, daemon=True).start()
                return True
            except Exception as e:
                pass

        # Fallback / standard method for Xbox 360 and generic EV_FF gamepads:
        if self.event_path:
            try:
                strong_val = int((strong_pct / 100.0) * 65535)
                weak_val   = int((weak_pct / 100.0) * 65535)

                fd = os.open(self.event_path, os.O_RDWR)
                effect_data = struct.pack('HhHHHHHHH', FF_RUMBLE, -1, 0, 0, 0, duration_ms, 0, strong_val, weak_val)
                effect_data += b'\x00' * 28  # pad union to 32 bytes (total 48 bytes)
                res = fcntl.ioctl(fd, EVIOCSFF, effect_data)
                eff_id = struct.unpack('Hh', res[:4])[1]
                self.active_effect_id = eff_id

                play_event = struct.pack('qqHHi', 0, 0, EV_FF, eff_id, 1)
                os.write(fd, play_event)
                os.close(fd)
                return True
            except Exception as e:
                print(f"Error sending rumble to {self.event_path}: {e}")
                return False
        return False

    def send_ds4_raw(self, motor_l=0, motor_r=0, led_r=None, led_g=None, led_b=None):
        """Sends USB Output Report 0x05 to Sony DualShock 4 via hidraw"""
        if not self.hidraw_path:
            return False
        fd = os.open(self.hidraw_path, os.O_RDWR)
        try:
            report = bytearray(32)
            report[0] = 0x05  # Output Report ID 5
            flags = 0x00
            if motor_l > 0 or motor_r > 0 or (led_r is None):
                flags |= 0x01  # Motor enable
            if led_r is not None:
                flags |= 0x02  # LED enable

            report[1] = flags
            report[4] = motor_r & 0xFF
            report[5] = motor_l & 0xFF
            if led_r is not None:
                report[6] = led_r & 0xFF
                report[7] = led_g & 0xFF
                report[8] = led_b & 0xFF
            os.write(fd, report)
            return True
        finally:
            os.close(fd)

    def set_ds4_color(self, r, g, b):
        """Set DualShock 4 Lightbar color (0..255)"""
        return self.send_ds4_raw(led_r=r, led_g=g, led_b=b)


def discover_gamepads():
    devices = []
    seen_events = set()

    # 1. Scan /dev/hidraw* for DualShock 4
    ds4_hidraw = {}
    for h in sorted(glob.glob('/dev/hidraw*')):
        try:
            fd = os.open(h, os.O_RDWR | os.O_NONBLOCK)
            buf = bytearray(256)
            fcntl.ioctl(fd, HIDIOCGRAWNAME, buf)
            name = buf.decode('utf-8', errors='ignore').split('\x00')[0]
            os.close(fd)
            if 'Wireless Controller' in name or 'Sony' in name or 'DualShock' in name:
                ds4_hidraw[name] = h
        except Exception:
            pass

    # 2. Scan /dev/input/event* for gamepads with Force Feedback
    for ev in sorted(glob.glob('/dev/input/event*')):
        try:
            fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
            buf = bytearray(256)
            fcntl.ioctl(fd, EVIOCGNAME, buf)
            name = buf.decode('utf-8', errors='ignore').split('\x00')[0]

            ev_bits = fcntl.ioctl(fd, EVIOCGBIT_EV, struct.pack('I', 0))
            ev_val = struct.unpack('I', ev_bits)[0]
            has_ff = bool(ev_val & (1 << EV_FF))
            os.close(fd)

            if has_ff:
                is_ds4 = ('Wireless Controller' in name or 'Sony' in name or 'DualShock' in name)
                is_xpad = ('Xbox' in name or 'X-Box' in name or 'Microsoft' in name)
                h_path = ds4_hidraw.get(name)

                # Avoid duplicate entries for motion sensor/touchpad input events
                if is_ds4 and ('Motion Sensors' in name or 'Touchpad' in name):
                    continue

                dev = GamepadDevice(name, event_path=ev, hidraw_path=h_path, is_ds4=is_ds4, is_xpad=is_xpad)
                devices.append(dev)
                seen_events.add(ev)
        except Exception:
            pass

    # If DS4 hidraw found without event match, add it
    for name, h_path in ds4_hidraw.items():
        if not any(d.hidraw_path == h_path for d in devices):
            devices.append(GamepadDevice(name, hidraw_path=h_path, is_ds4=True))

    return devices


# ==============================================================================
# Interactive Terminal (CLI) Interface
# ==============================================================================
def run_cli(devices):
    if not devices:
        print("\n[!] No compatible gamepads with Force Feedback found!")
        print("    Make sure your controller is connected via USB and recognized by Linux.")
        return

    while True:
        print("\n" + "="*60)
        print("          GAMEPAD TESTER (DualShock 4 & Xbox 360)")
        print("="*60)
        print("Connected Devices:")
        for idx, dev in enumerate(devices):
            info = []
            if dev.event_path: info.append(dev.event_path)
            if dev.hidraw_path: info.append(dev.hidraw_path)
            mode_tag = "[DS4]" if dev.is_ds4 else ("[Xbox 360]" if dev.is_xpad else "[Gamepad]")
            print(f"  [{idx + 1}] {mode_tag} {dev.name} ({', '.join(info)})")

        dev_idx = 0
        if len(devices) > 1:
            try:
                choice = input(f"\nSelect Gamepad (1-{len(devices)}, or 'q' to quit) [1]: ").strip()
                if choice.lower() == 'q':
                    return
                if choice.isdigit() and 1 <= int(choice) <= len(devices):
                    dev_idx = int(choice) - 1
            except (KeyboardInterrupt, EOFError):
                return
        dev = devices[dev_idx]

        print(f"\nSelected: {dev.name}")
        print("Commands:")
        print("  1: Test Left Motor  (Heavy Rumble / RED LED)  [1.0 sec, 100%]")
        print("  2: Test Right Motor (Soft Rumble  / BLUE LED) [1.0 sec, 100%]")
        print("  3: Test Both Motors (Combined     / PURPLE)   [1.0 sec, 100%]")
        print("  4: Custom Vibration (Adjust Left & Right %, duration)")
        if dev.is_ds4:
            print("  5: DS4 Lightbar Color Picker (Red, Green, Blue, Yellow, Cyan, White, Custom)")
        print("  6: Monitor Inputs (Real-time live buttons and axes)")
        print("  r: Rescan Devices")
        print("  q: Exit")

        try:
            cmd = input("\nEnter command: ").strip().lower()
        except (KeyboardInterrupt, EOFError):
            break

        if cmd == 'q':
            break
        elif cmd == 'r':
            devices = discover_gamepads()
            continue
        elif cmd == '1':
            print(">> Vibrating LEFT motor (Heavy -> RED LED) for 1 second...")
            dev.send_rumble(strong_pct=100, weak_pct=0, duration_ms=1000)
            time.sleep(1.0)
        elif cmd == '2':
            print(">> Vibrating RIGHT motor (Soft -> BLUE LED) for 1 second...")
            dev.send_rumble(strong_pct=0, weak_pct=100, duration_ms=1000)
            time.sleep(1.0)
        elif cmd == '3':
            print(">> Vibrating BOTH motors (Purple LED) for 1 second...")
            dev.send_rumble(strong_pct=100, weak_pct=100, duration_ms=1000)
            time.sleep(1.0)
        elif cmd == '4':
            try:
                s = int(input("Left motor intensity (0-100) [100]: ") or "100")
                w = int(input("Right motor intensity (0-100) [100]: ") or "100")
                d = float(input("Duration in seconds [1.5]: ") or "1.5")
                print(f">> Vibrating Left={s}%, Right={w}% for {d}s...")
                dev.send_rumble(strong_pct=s, weak_pct=w, duration_ms=int(d * 1000))
                time.sleep(d)
            except ValueError:
                print("Invalid input!")
        elif cmd == '5' and dev.is_ds4:
            print("\nDS4 Lightbar Color Presets:")
            print("  1: 🔴 Pure Red     (255, 0, 0)")
            print("  2: 🟢 Pure Green   (0, 255, 0)")
            print("  3: 🔵 Pure Blue    (0, 0, 255)")
            print("  4: 🟡 Yellow       (255, 255, 0)")
            print("  5: 🟣 Magenta      (255, 0, 255)")
            print("  6: 🔷 Cyan         (0, 255, 255)")
            print("  7: ⚪ White        (255, 255, 255)")
            print("  8: ⚫ Off / Black  (0, 0, 0)")
            print("  9: Custom RGB (R G B)")
            c_choice = input("Select color preset [1]: ").strip()
            color_map = {
                '1': (255, 0, 0),
                '2': (0, 255, 0),
                '3': (0, 0, 255),
                '4': (255, 255, 0),
                '5': (255, 0, 255),
                '6': (0, 255, 255),
                '7': (255, 255, 255),
                '8': (0, 0, 0)
            }
            if c_choice in color_map:
                r, g, b = color_map[c_choice]
            elif c_choice == '9':
                parts = input("Enter R G B values (0-255 each, e.g. '128 0 255'): ").split()
                r, g, b = int(parts[0]), int(parts[1]), int(parts[2])
            else:
                r, g, b = (255, 0, 0)
            print(f">> Sending Lightbar RGB ({r}, {g}, {b}) to DS4...")
            dev.set_ds4_color(r, g, b)
        elif cmd == '6':
            run_input_monitor(dev)


def run_input_monitor(dev):
    if not dev.event_path:
        print("Cannot monitor inputs: no /dev/input/event* path for this device.")
        return
    print(f"\nMonitoring inputs on {dev.event_path} (Press Ctrl+C to stop)...")
    try:
        fd = os.open(dev.event_path, os.O_RDONLY)
        # Event format: timeval (16 bytes) + u16 type + u16 code + s32 value = 24 bytes
        while True:
            data = os.read(fd, 24)
            if not data or len(data) < 24:
                continue
            sec, usec, ev_type, ev_code, ev_val = struct.unpack('qqHHi', data)
            if ev_type == EV_KEY:
                action = "PRESSED " if ev_val == 1 else "RELEASED"
                print(f"[BUTTON] Code: 0x{ev_code:03X} ({ev_code:3d}) -> {action}")
            elif ev_type == EV_ABS:
                print(f"[AXIS]   Code: 0x{ev_code:02X} ({ev_code:2d}) -> Value: {ev_val:6d}")
    except KeyboardInterrupt:
        print("\nStopped input monitoring.")
    finally:
        os.close(fd)


# ==============================================================================
# Tkinter Graphical User Interface (GUI)
# ==============================================================================
def run_gui(devices):
    import tkinter as tk
    from tkinter import ttk, messagebox

    root = tk.Tk()
    root.title("Gamepad Tester - Rumble & Lightbar (DS4 / Xbox 360)")
    root.geometry("620x540")
    root.resizable(False, False)

    # Frame: Device Selection
    dev_frame = ttk.LabelFrame(root, text=" Target Gamepad ", padding=10)
    dev_frame.pack(fill="x", padx=12, pady=8)

    dev_var = tk.StringVar()
    dev_combo = ttk.Combobox(dev_frame, textvariable=dev_var, state="readonly", width=65)
    dev_combo.pack(side="left", padx=5)

    def refresh_devices():
        nonlocal devices
        devices = discover_gamepads()
        if not devices:
            dev_combo['values'] = ["No compatible gamepads found"]
            dev_combo.current(0)
            return
        dev_combo['values'] = [f"{d.name} ({d.event_path or d.hidraw_path})" for d in devices]
        dev_combo.current(0)

    ttk.Button(dev_frame, text="↻ Refresh", command=refresh_devices).pack(side="left", padx=5)
    refresh_devices()

    def get_selected_dev():
        idx = dev_combo.current()
        if idx >= 0 and idx < len(devices):
            return devices[idx]
        return None

    # Frame: Vibration / Rumble
    rumble_frame = ttk.LabelFrame(root, text=" Force Feedback / Vibration Test ", padding=10)
    rumble_frame.pack(fill="x", padx=12, pady=8)

    # Left Motor (Heavy / Red)
    ttk.Label(rumble_frame, text="Left Motor (Heavy / Low Freq -> RED LED):").grid(row=0, column=0, sticky="w", pady=4)
    left_slider = ttk.Scale(rumble_frame, from_=0, to=100, orient="horizontal", length=220)
    left_slider.set(100)
    left_slider.grid(row=0, column=1, padx=10, pady=4)
    left_val_lbl = ttk.Label(rumble_frame, text="100%")
    left_val_lbl.grid(row=0, column=2, padx=5)
    left_slider.configure(command=lambda v: left_val_lbl.config(text=f"{int(float(v))}%"))

    # Right Motor (Soft / Blue)
    ttk.Label(rumble_frame, text="Right Motor (Soft / High Freq -> BLUE LED):").grid(row=1, column=0, sticky="w", pady=4)
    right_slider = ttk.Scale(rumble_frame, from_=0, to=100, orient="horizontal", length=220)
    right_slider.set(100)
    right_slider.grid(row=1, column=1, padx=10, pady=4)
    right_val_lbl = ttk.Label(rumble_frame, text="100%")
    right_val_lbl.grid(row=1, column=2, padx=5)
    right_slider.configure(command=lambda v: right_val_lbl.config(text=f"{int(float(v))}%"))

    # Action Buttons
    btn_box = ttk.Frame(rumble_frame)
    btn_box.grid(row=2, column=0, columnspan=3, pady=12)

    def trigger_left():
        d = get_selected_dev()
        if d: d.send_rumble(strong_pct=int(left_slider.get()), weak_pct=0, duration_ms=1000)

    def trigger_right():
        d = get_selected_dev()
        if d: d.send_rumble(strong_pct=0, weak_pct=int(right_slider.get()), duration_ms=1000)

    def trigger_both():
        d = get_selected_dev()
        if d: d.send_rumble(strong_pct=int(left_slider.get()), weak_pct=int(right_slider.get()), duration_ms=1000)

    def trigger_stop():
        d = get_selected_dev()
        if d: d.send_rumble(strong_pct=0, weak_pct=0, duration_ms=100)

    ttk.Button(btn_box, text="🔴 Pulse Left (1s)", command=trigger_left).pack(side="left", padx=6)
    ttk.Button(btn_box, text="🔵 Pulse Right (1s)", command=trigger_right).pack(side="left", padx=6)
    ttk.Button(btn_box, text="🟣 Pulse Both (1s)", command=trigger_both).pack(side="left", padx=6)
    ttk.Button(btn_box, text="⏹ Stop", command=trigger_stop).pack(side="left", padx=6)

    # Frame: DualShock 4 Lightbar
    ds4_frame = ttk.LabelFrame(root, text=" Sony DualShock 4 Lightbar RGB Control ", padding=10)
    ds4_frame.pack(fill="x", padx=12, pady=8)

    preview_canvas = tk.Canvas(ds4_frame, width=54, height=54, bg="#FFFFFF", highlightthickness=2, highlightbackground="#555")
    preview_canvas.grid(row=0, column=3, rowspan=2, padx=16, pady=4)

    live_var = tk.BooleanVar(value=True)

    def set_color_hardware(r, g, b):
        hex_col = f"#{r:02x}{g:02x}{b:02x}"
        preview_canvas.config(bg=hex_col)
        d = get_selected_dev()
        if d and d.is_ds4:
            d.set_ds4_color(r, g, b)

    preset_box = ttk.Frame(ds4_frame)
    preset_box.grid(row=0, column=0, columnspan=3, pady=6, sticky="w")

    # RGB Slider Labels & Controls
    rgb_box = ttk.Frame(ds4_frame)
    rgb_box.grid(row=1, column=0, columnspan=3, pady=4, sticky="w")

    r_val_lbl = ttk.Label(rgb_box, text="255", width=4)
    g_val_lbl = ttk.Label(rgb_box, text="255", width=4)
    b_val_lbl = ttk.Label(rgb_box, text="255", width=4)

    def on_r_change(val):
        r = int(float(val))
        r_val_lbl.config(text=str(r))
        if live_var.get():
            set_color_hardware(r, int(float(g_slider.get())), int(float(b_slider.get())))

    def on_g_change(val):
        g = int(float(val))
        g_val_lbl.config(text=str(g))
        if live_var.get():
            set_color_hardware(int(float(r_slider.get())), g, int(float(b_slider.get())))

    def on_b_change(val):
        b = int(float(val))
        b_val_lbl.config(text=str(b))
        if live_var.get():
            set_color_hardware(int(float(r_slider.get())), int(float(g_slider.get())), b)

    # Row 0: Red
    ttk.Label(rgb_box, text="Red:").grid(row=0, column=0, sticky="w", padx=2, pady=2)
    r_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220, command=on_r_change)
    r_slider.set(255)
    r_slider.grid(row=0, column=1, padx=6, pady=2)
    r_val_lbl.grid(row=0, column=2, padx=4)

    # Row 1: Green
    ttk.Label(rgb_box, text="Green:").grid(row=1, column=0, sticky="w", padx=2, pady=2)
    g_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220, command=on_g_change)
    g_slider.set(255)
    g_slider.grid(row=1, column=1, padx=6, pady=2)
    g_val_lbl.grid(row=1, column=2, padx=4)

    # Row 2: Blue
    ttk.Label(rgb_box, text="Blue:").grid(row=2, column=0, sticky="w", padx=2, pady=2)
    b_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220, command=on_b_change)
    b_slider.set(255)
    b_slider.grid(row=2, column=1, padx=6, pady=2)
    b_val_lbl.grid(row=2, column=2, padx=4)

    def update_ds4_color(r, g, b):
        r_slider.set(r)
        g_slider.set(g)
        b_slider.set(b)
        r_val_lbl.config(text=str(r))
        g_val_lbl.config(text=str(g))
        b_val_lbl.config(text=str(b))
        set_color_hardware(r, g, b)

    ttk.Button(preset_box, text="🔴 Red", width=7, command=lambda: update_ds4_color(255, 0, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟢 Green", width=7, command=lambda: update_ds4_color(0, 255, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🔵 Blue", width=7, command=lambda: update_ds4_color(0, 0, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟡 Yellow", width=7, command=lambda: update_ds4_color(255, 255, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟣 Magenta", width=7, command=lambda: update_ds4_color(255, 0, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🔷 Cyan", width=7, command=lambda: update_ds4_color(0, 255, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="⚪ White", width=7, command=lambda: update_ds4_color(255, 255, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="⚫ Off", width=7, command=lambda: update_ds4_color(0, 0, 0)).pack(side="left", padx=2)

    # Actions: Live update checkbox & Apply button
    ctrl_bar = ttk.Frame(ds4_frame)
    ctrl_bar.grid(row=2, column=0, columnspan=4, pady=6, sticky="w")

    def on_apply_click():
        r = int(float(r_slider.get()))
        g = int(float(g_slider.get()))
        b = int(float(b_slider.get()))
        set_color_hardware(r, g, b)

    ttk.Checkbutton(ctrl_bar, text="Live update on drag", variable=live_var).pack(side="left", padx=5)
    ttk.Button(ctrl_bar, text="Apply Color", command=on_apply_click).pack(side="left", padx=10)

    root.mainloop()


# ==============================================================================
# Main Entry Point
# ==============================================================================
def main():
    parser = argparse.ArgumentParser(description="Gamepad Vibration & Lightbar Tester (DualShock 4 & Xbox 360)")
    parser.add_argument("--cli", action="store_true", help="Force interactive terminal (CLI) mode")
    parser.add_argument("--gui", action="store_true", help="Force graphical (Tkinter) mode")
    parser.add_argument("--rumble-left", type=int, metavar="PCT", help="Vibrate Left motor (0-100%%) for 1 second")
    parser.add_argument("--rumble-right", type=int, metavar="PCT", help="Vibrate Right motor (0-100%%) for 1 second")
    parser.add_argument("--rumble", nargs=2, type=int, metavar=("LEFT", "RIGHT"), help="Vibrate both motors (0-100%%)")
    parser.add_argument("--duration", type=float, default=1.0, help="Vibration duration in seconds (default: 1.0)")
    parser.add_argument("--color", nargs=3, type=int, metavar=("R", "G", "B"), help="Set DS4 Lightbar RGB color (0-255)")
    args = parser.parse_args()

    devices = discover_gamepads()

    # One-liner CLI arguments
    if args.rumble_left is not None or args.rumble_right is not None or args.rumble is not None or args.color is not None:
        if not devices:
            print("[!] Error: No compatible gamepads detected.")
            sys.exit(1)
        dev = devices[0]
        if args.color is not None:
            r, g, b = args.color
            print(f">> Setting Lightbar color ({r}, {g}, {b}) on {dev.name}...")
            dev.set_ds4_color(r, g, b)
        if args.rumble is not None:
            s, w = args.rumble
            print(f">> Vibrating Left={s}%, Right={w}% for {args.duration}s on {dev.name}...")
            dev.send_rumble(strong_pct=s, weak_pct=w, duration_ms=int(args.duration * 1000))
            time.sleep(args.duration)
        elif args.rumble_left is not None:
            print(f">> Vibrating Left={args.rumble_left}% for {args.duration}s on {dev.name}...")
            dev.send_rumble(strong_pct=args.rumble_left, weak_pct=0, duration_ms=int(args.duration * 1000))
            time.sleep(args.duration)
        elif args.rumble_right is not None:
            print(f">> Vibrating Right={args.rumble_right}% for {args.duration}s on {dev.name}...")
            dev.send_rumble(strong_pct=0, weak_pct=args.rumble_right, duration_ms=int(args.duration * 1000))
            time.sleep(args.duration)
        sys.exit(0)

    # Launch GUI if possible and requested/defaulted
    has_display = bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))
    if not args.cli and (args.gui or (has_display and not args.cli)):
        try:
            run_gui(devices)
            return
        except Exception as e:
            # Fallback to CLI if GUI fails
            pass

    # Default to interactive CLI
    run_cli(devices)


if __name__ == "__main__":
    main()

