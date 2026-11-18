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


def find_xpad_led_path(event_path):
    """Locates the sysfs brightness path for an Xbox 360 controller's xpad LED."""
    if not event_path:
        return None
    try:
        ev_name = os.path.basename(event_path)
        dev_sys = os.path.realpath(f'/sys/class/input/{ev_name}/device')
        p = dev_sys
        while p and p != '/':
            cand = os.path.join(p, 'leds')
            if os.path.isdir(cand):
                leds = [x for x in os.listdir(cand) if x.startswith('xpad')]
                if leds:
                    return os.path.join(cand, leds[0], 'brightness')
            p = os.path.dirname(p)
    except Exception:
        pass
    return None


class GamepadDevice:
    def __init__(self, name, event_path=None, hidraw_path=None, is_ds4=False, is_xpad=False, is_switch=False):
        self.name = name
        self.event_path = event_path
        self.hidraw_path = hidraw_path
        self.is_ds4 = is_ds4
        self.is_xpad = is_xpad
        self.is_switch = is_switch
        self._ff_fd = None
        self.active_effect_id = -1
        self._ff_stop_timer = None
        self.xpad_led_path = find_xpad_led_path(event_path) if is_xpad else None

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
            except Exception:
                pass

        # Standard Linux evdev Force Feedback (for Xbox 360 and generic EV_FF gamepads):
        if self.event_path:
            try:
                if self._ff_fd is None:
                    self._ff_fd = os.open(self.event_path, os.O_RDWR)

                if self._ff_stop_timer and self._ff_stop_timer.is_alive():
                    self._ff_stop_timer.cancel()

                if strong_pct == 0 and weak_pct == 0:
                    if self.active_effect_id != -1:
                        stop_event = struct.pack('qqHHi', 0, 0, EV_FF, self.active_effect_id, 0)
                        os.write(self._ff_fd, stop_event)
                    return True

                strong_val = int((strong_pct / 100.0) * 65535)
                weak_val   = int((weak_pct / 100.0) * 65535)

                # Exact 48-byte struct ff_effect on 64-bit Linux:
                # type(H=2) + id(h=2) + direction(H=2) + button(H=2) + interval(H=2) +
                # length(H=2) + delay(H=2) + 2x(pad=2) + strong(H=2) + weak(H=2) + 28s(union pad=28) = 48 bytes
                effect_data = struct.pack(
                    'HhHHHHH2xHH28s',
                    FF_RUMBLE,
                    self.active_effect_id,
                    0, 0, 0,
                    duration_ms, 0,
                    strong_val, weak_val,
                    b'\x00' * 28
                )
                res = fcntl.ioctl(self._ff_fd, EVIOCSFF, effect_data)
                eff_id = struct.unpack('Hh', res[:4])[1]
                self.active_effect_id = eff_id

                play_event = struct.pack('qqHHi', 0, 0, EV_FF, self.active_effect_id, 1)
                os.write(self._ff_fd, play_event)

                if duration_ms > 0:
                    def _stop_worker():
                        time.sleep(duration_ms / 1000.0)
                        if self._ff_fd is not None and self.active_effect_id != -1:
                            try:
                                stop_ev = struct.pack('qqHHi', 0, 0, EV_FF, self.active_effect_id, 0)
                                os.write(self._ff_fd, stop_ev)
                            except Exception:
                                pass
                    self._ff_stop_timer = threading.Thread(target=_stop_worker, daemon=True)
                    self._ff_stop_timer.start()
                return True
            except Exception as e:
                print(f"Error sending rumble to {self.event_path}: {e}")
                return False
        return False

    def close(self):
        """Releases force feedback resources and closes device descriptors."""
        if self._ff_fd is not None:
            if self.active_effect_id != -1:
                try:
                    os.write(self._ff_fd, struct.pack('qqHHi', 0, 0, EV_FF, self.active_effect_id, 0))
                except Exception:
                    pass
            try:
                os.close(self._ff_fd)
            except Exception:
                pass
            self._ff_fd = None

    def __del__(self):
        self.close()

    def set_xpad_led(self, pattern):
        """Sets Xbox 360 Ring of Light pattern (0..14) via sysfs."""
        if not self.xpad_led_path:
            return False, "xpad sysfs LED path not found"
        try:
            with open(self.xpad_led_path, 'w') as f:
                f.write(f"{pattern}\n")
            return True, "OK"
        except (PermissionError, OSError) as e:
            return False, f"Cannot write to {self.xpad_led_path} ({e.strerror or e}).\nTo enable, run: sudo chmod 666 /sys/class/leds/xpad*/brightness"

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

            if has_ff or ('POKKEN' in name or 'Nintendo' in name or 'Switch' in name or 'HORI' in name):
                is_ds4 = ('Wireless Controller' in name or 'Sony' in name or 'DualShock' in name)
                is_xpad = ('Xbox' in name or 'X-Box' in name or 'Microsoft' in name)
                is_switch = ('POKKEN' in name or 'Nintendo' in name or 'Switch' in name or 'HORI' in name)
                h_path = ds4_hidraw.get(name)

                # Avoid duplicate entries for motion sensor/touchpad input events
                if is_ds4 and ('Motion Sensors' in name or 'Touchpad' in name):
                    continue

                dev = GamepadDevice(name, event_path=ev, hidraw_path=h_path, is_ds4=is_ds4, is_xpad=is_xpad, is_switch=is_switch)
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
        print("\n[!] No compatible gamepads found!")
        print("    Make sure your controller is connected via USB and recognized by Linux.")
        return

    while True:
        print("\n" + "="*60)
        print("      GAMEPAD TESTER (DualShock 4 / Xbox 360 / Nintendo)")
        print("="*60)
        print("Connected Devices:")
        for idx, dev in enumerate(devices):
            info = []
            if dev.event_path: info.append(dev.event_path)
            if dev.hidraw_path: info.append(dev.hidraw_path)
            mode_tag = "[DS4]" if dev.is_ds4 else ("[Xbox 360]" if dev.is_xpad else ("[Switch]" if dev.is_switch else "[Gamepad]"))
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
        elif dev.is_xpad:
            print("  5: Xbox 360 Ring of Light LED Patterns (Player 1-4, Rotating, Blink, etc.)")
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
            for d in devices:
                d.close()
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
        elif cmd == '5' and dev.is_xpad:
            print("\nXbox 360 Ring of Light Patterns:")
            print("  0: ⚫ All Off")
            print("  1: ✨ All Blink")
            print("  2: 1️⃣ Player 1 (Top-Left)")
            print("  3: 2️⃣ Player 2 (Top-Right)")
            print("  4: 3️⃣ Player 3 (Bottom-Left)")
            print("  5: 4️⃣ Player 4 (Bottom-Right)")
            print(" 10: 🔄 Rotating")
            print(" 11: ⚡ Fast Blink")
            print(" 12: ⏳ Slow Blink")
            print(" 13: 🔀 Alternating")
            try:
                p_in = input("Enter pattern number (0-14) [2]: ").strip() or "2"
                p_val = int(p_in)
                ok, msg = dev.set_xpad_led(p_val)
                if ok:
                    print(f">> Successfully applied pattern {p_val} to Xbox 360 LED!")
                else:
                    print(f"[!] {msg}")
            except ValueError:
                print("Invalid pattern number!")
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
    root.geometry("680x560")
    root.resizable(False, False)

    # Frame: Device Selection
    dev_frame = ttk.LabelFrame(root, text=" Target Gamepad ", padding=10)
    dev_frame.pack(fill="x", padx=12, pady=8)

    dev_var = tk.StringVar()
    dev_combo = ttk.Combobox(dev_frame, textvariable=dev_var, state="readonly", width=65)
    dev_combo.pack(side="left", padx=5)

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

    # Dynamic Container for Controller LEDs
    led_container = ttk.Frame(root)
    led_container.pack(fill="x", padx=12, pady=4)

    # -------------------------------------------------------------
    # 1. Sony DualShock 4 Lightbar Frame
    # -------------------------------------------------------------
    ds4_frame = ttk.LabelFrame(led_container, text=" Sony DualShock 4 Lightbar RGB Control ", padding=10)

    preview_canvas = tk.Canvas(ds4_frame, width=54, height=54, bg="#FFFFFF", highlightthickness=2, highlightbackground="#555")
    preview_canvas.grid(row=0, column=2, rowspan=3, padx=16, pady=4)

    live_var = tk.BooleanVar(value=True)

    def set_color_hardware(r, g, b):
        hex_col = f"#{r:02x}{g:02x}{b:02x}"
        preview_canvas.config(bg=hex_col)
        d = get_selected_dev()
        if d and d.is_ds4:
            d.set_ds4_color(r, g, b)

    preset_box = ttk.Frame(ds4_frame)
    preset_box.grid(row=0, column=0, columnspan=3, pady=6, sticky="w")

    rgb_box = ttk.Frame(ds4_frame)
    rgb_box.grid(row=1, column=0, columnspan=3, pady=4, sticky="w")

    r_val_lbl = ttk.Label(rgb_box, text="5", width=4)
    g_val_lbl = ttk.Label(rgb_box, text="5", width=4)
    b_val_lbl = ttk.Label(rgb_box, text="5", width=4)

    # Row 0: Red
    ttk.Label(rgb_box, text="Red:").grid(row=0, column=0, sticky="w", padx=2, pady=2)
    r_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220)
    r_slider.set(5)
    r_slider.grid(row=0, column=1, padx=6, pady=2)
    r_val_lbl.grid(row=0, column=2, padx=4)

    # Row 1: Green
    ttk.Label(rgb_box, text="Green:").grid(row=1, column=0, sticky="w", padx=2, pady=2)
    g_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220)
    g_slider.set(5)
    g_slider.grid(row=1, column=1, padx=6, pady=2)
    g_val_lbl.grid(row=1, column=2, padx=4)

    # Row 2: Blue
    ttk.Label(rgb_box, text="Blue:").grid(row=2, column=0, sticky="w", padx=2, pady=2)
    b_slider = ttk.Scale(rgb_box, from_=0, to=255, orient="horizontal", length=220)
    b_slider.set(5)
    b_slider.grid(row=2, column=1, padx=6, pady=2)
    b_val_lbl.grid(row=2, column=2, padx=4)

    is_updating = False

    def on_color_change(*_):
        if is_updating:
            return
        r = int(float(r_slider.get()))
        g = int(float(g_slider.get()))
        b = int(float(b_slider.get()))
        r_val_lbl.config(text=str(r))
        g_val_lbl.config(text=str(g))
        b_val_lbl.config(text=str(b))
        if live_var.get():
            set_color_hardware(r, g, b)

    r_slider.configure(command=on_color_change)
    g_slider.configure(command=on_color_change)
    b_slider.configure(command=on_color_change)

    def update_ds4_color(r, g, b):
        nonlocal is_updating
        is_updating = True
        try:
            r_slider.set(r)
            g_slider.set(g)
            b_slider.set(b)
            r_val_lbl.config(text=str(r))
            g_val_lbl.config(text=str(g))
            b_val_lbl.config(text=str(b))
        finally:
            is_updating = False
        set_color_hardware(r, g, b)

    ttk.Button(preset_box, text="🔴 Red", width=7, command=lambda: update_ds4_color(255, 0, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟢 Green", width=7, command=lambda: update_ds4_color(0, 255, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🔵 Blue", width=7, command=lambda: update_ds4_color(0, 0, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟡 Yellow", width=7, command=lambda: update_ds4_color(255, 255, 0)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🟣 Magenta", width=7, command=lambda: update_ds4_color(255, 0, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="🔷 Cyan", width=7, command=lambda: update_ds4_color(0, 255, 255)).pack(side="left", padx=2)
    ttk.Button(preset_box, text="⚪ White", width=7, command=lambda: update_ds4_color(255, 255, 255)).pack(side="left", padx=2)

    ctrl_bar = ttk.Frame(ds4_frame)
    ctrl_bar.grid(row=2, column=0, columnspan=4, pady=6, sticky="w")

    def on_apply_click():
        r = int(float(r_slider.get()))
        g = int(float(g_slider.get()))
        b = int(float(b_slider.get()))
        set_color_hardware(r, g, b)

    ttk.Checkbutton(ctrl_bar, text="Live update on drag", variable=live_var).pack(side="left", padx=5)
    ttk.Button(ctrl_bar, text="Apply Color", command=on_apply_click).pack(side="left", padx=10)
    ttk.Button(ctrl_bar, text="⚫ Off", width=7, command=lambda: update_ds4_color(0, 0, 0)).pack(side="left", padx=15)

    # -------------------------------------------------------------
    # 2. Microsoft Xbox 360 Ring of Light Frame
    # -------------------------------------------------------------
    xpad_frame = ttk.LabelFrame(led_container, text=" Microsoft Xbox 360 Ring of Light LED Patterns ", padding=10)

    xpad_btn_box = ttk.Frame(xpad_frame)
    xpad_btn_box.pack(fill="x", pady=4)

    def apply_xpad_pattern(pattern):
        d = get_selected_dev()
        if d and d.is_xpad:
            ok, msg = d.set_xpad_led(pattern)
            if ok:
                xpad_status_lbl.config(text=f"Pattern {pattern} applied successfully!", foreground="green")
            else:
                xpad_status_lbl.config(text=f"⚠️ {msg}", foreground="#c9302c")

    p_box = ttk.Frame(xpad_btn_box)
    p_box.pack(anchor="w", pady=3)
    ttk.Label(p_box, text="Players:").pack(side="left", padx=4)
    ttk.Button(p_box, text="1️⃣ Player 1", width=10, command=lambda: apply_xpad_pattern(2)).pack(side="left", padx=3)
    ttk.Button(p_box, text="2️⃣ Player 2", width=10, command=lambda: apply_xpad_pattern(3)).pack(side="left", padx=3)
    ttk.Button(p_box, text="3️⃣ Player 3", width=10, command=lambda: apply_xpad_pattern(4)).pack(side="left", padx=3)
    ttk.Button(p_box, text="4️⃣ Player 4", width=10, command=lambda: apply_xpad_pattern(5)).pack(side="left", padx=3)
    ttk.Button(p_box, text="⚫ Off", width=7, command=lambda: apply_xpad_pattern(0)).pack(side="left", padx=8)

    a_box = ttk.Frame(xpad_btn_box)
    a_box.pack(anchor="w", pady=3)
    ttk.Label(a_box, text="Modes:  ").pack(side="left", padx=4)
    ttk.Button(a_box, text="🔄 Rotating", width=11, command=lambda: apply_xpad_pattern(10)).pack(side="left", padx=3)
    ttk.Button(a_box, text="⚡ Fast Blink", width=12, command=lambda: apply_xpad_pattern(11)).pack(side="left", padx=3)
    ttk.Button(a_box, text="⏳ Slow Blink", width=12, command=lambda: apply_xpad_pattern(12)).pack(side="left", padx=3)
    ttk.Button(a_box, text="🔀 Alternating", width=12, command=lambda: apply_xpad_pattern(13)).pack(side="left", padx=3)

    xpad_status_lbl = ttk.Label(xpad_frame, text="", wraplength=640)
    xpad_status_lbl.pack(anchor="w", padx=4, pady=4)

    # -------------------------------------------------------------
    # 3. Generic Frame (fallback when device has no LEDs)
    # -------------------------------------------------------------
    generic_frame = ttk.LabelFrame(led_container, text=" Controller LED Status ", padding=10)
    generic_status_lbl = ttk.Label(generic_frame, text="No LED controls supported for this controller profile.")
    generic_status_lbl.pack(pady=10)

    # Switch visible LED panel based on selected device profile
    def update_active_panel(*_):
        d = get_selected_dev()
        ds4_frame.pack_forget()
        xpad_frame.pack_forget()
        generic_frame.pack_forget()
        if d:
            if d.is_ds4:
                ds4_frame.pack(fill="x")
            elif d.is_xpad:
                xpad_frame.pack(fill="x")
                if d.xpad_led_path:
                    xpad_status_lbl.config(text=f"LED node: {d.xpad_led_path}", foreground="#555")
                else:
                    xpad_status_lbl.config(text="⚠️ No xpad LED node found in /sys/class/leds/.", foreground="#c9302c")
            elif d.is_switch:
                generic_frame.pack(fill="x")
                generic_status_lbl.config(text="🎮 Nintendo Switch Mode (HORI Pokken Protocol: Plug & Play on Switch, Steam, PC)")
            else:
                generic_frame.pack(fill="x")
                generic_status_lbl.config(text="No LED controls supported for this controller profile.")

    dev_combo.bind("<<ComboboxSelected>>", update_active_panel)

    def refresh_devices():
        nonlocal devices
        for d in devices:
            d.close()
        devices = discover_gamepads()
        if not devices:
            dev_combo['values'] = ["No compatible gamepads found"]
            dev_combo.current(0)
            update_active_panel()
            return
        dev_combo['values'] = [f"{d.name} ({d.event_path or d.hidraw_path})" for d in devices]
        dev_combo.current(0)
        update_active_panel()

    ttk.Button(dev_frame, text="↻ Refresh", command=refresh_devices).pack(side="left", padx=5)
    refresh_devices()

    def on_closing():
        for d in devices:
            d.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_closing)
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
    parser.add_argument("--xpad-led", type=int, metavar="PATTERN", help="Set Xbox 360 Ring of Light pattern (0-14)")
    args = parser.parse_args()

    devices = discover_gamepads()

    # One-liner CLI arguments
    if (args.rumble_left is not None or args.rumble_right is not None or 
        args.rumble is not None or args.color is not None or args.xpad_led is not None):
        if not devices:
            print("[!] Error: No compatible gamepads detected.")
            sys.exit(1)
        dev = devices[0]
        if args.xpad_led is not None:
            ok, msg = dev.set_xpad_led(args.xpad_led)
            if ok:
                print(f">> Applied pattern {args.xpad_led} to Xbox 360 LED on {dev.name}!")
            else:
                print(f"[!] {msg}")
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

