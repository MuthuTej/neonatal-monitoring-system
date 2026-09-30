# Wiring Guide

Complete connection reference for the AI-Powered Smart Neonatal Monitoring
System prototype.

> **Prototype notice.** This document describes a breadboard / perfboard
> academic build. It is not a certified medical-device design and carries no
> patient-isolation, leakage-current or fault-tolerance provisions.

---

## 1. Pin map at a glance

| ESP32 GPIO | Direction | Connected to | Signal |
|---|---|---|---|
| GPIO 21 | Bidirectional | MAX30102, MPU6050, OLED | I2C SDA (shared) |
| GPIO 22 | Output | MAX30102, MPU6050, OLED | I2C SCL (shared) |
| GPIO 4  | Bidirectional | DS18B20 | OneWire DATA |
| GPIO 34 | Input only | MAX9814 | Analog audio envelope |
| GPIO 25 | Output | Active buzzer | Alarm drive |
| GPIO 26 | Output | Red LED | Warning / alert |
| GPIO 27 | Output | Green LED | Normal status |
| 3V3 | Power | MAX30102, MPU6050, OLED, DS18B20, MAX9814 | 3.3 V rail |
| GND | Power | All modules | Common ground |

All of these values are defined once, in [`include/config.h`](../include/config.h).

---

## 2. Shared I2C bus

Three devices sit on **one** I2C bus. They are wired **in parallel**: every
device's SDA pin goes to GPIO 21 and every device's SCL pin goes to GPIO 22.
They are distinguished purely by their 7-bit addresses, which do not collide.

```
ESP32 GPIO 21 (SDA) ─────┬──────────────┬──────────────┐
                         │              │              │
ESP32 GPIO 22 (SCL) ───┬─│────────────┬─│────────────┬─│
                       │ │            │ │            │ │
                     ┌─┴─┴──┐      ┌──┴─┴──┐     ┌───┴─┴──┐
                     │MAX301│      │MPU6050│     │ SH1106 │
                     │  02  │      │       │     │  OLED  │
                     │ 0x57 │      │ 0x68  │     │  0x3C  │
                     └──────┘      └───────┘     └────────┘
```

| Device | Address | Role |
|---|---|---|
| MAX30102 | `0x57` | Heart rate, SpO2 |
| MPU6050 | `0x68` | Accelerometer, gyroscope |
| SH1106 OLED | `0x3C` | 128x64 display |

**Pull-up resistors.** All three breakout boards ship with on-board pull-ups
(typically 4.7 kΩ). Three sets in parallel give an effective pull-up of about
1.6 kΩ, which is still within the I2C specification at 400 kHz. If the bus
proves unreliable on a long-wired build, remove the pull-ups from two of the
three boards.

**Address conflicts.** The MPU6050 ADDR/AD0 pin must be tied **low** (or left
floating, since most breakouts pull it low) to keep it at `0x68`. Pulling it
high moves it to `0x69`, which would not match `I2C_ADDR_MPU6050`.

---

## 3. MAX30102 — Pulse oximeter

| Module pin | ESP32 pin | Note |
|---|---|---|
| VIN / VCC | 3V3 | Most breakouts include a 1.8 V LDO; feed them 3.3 V |
| GND | GND | |
| SDA | GPIO 21 | Shared bus |
| SCL | GPIO 22 | Shared bus |
| INT | *not connected* | The firmware polls the FIFO instead |
| RD / IRD | *not connected* | Internal LED cathodes, leave alone |

Placement: the optical window must sit flat against skin with no air gap and
no ambient light leaking in. On this prototype the sensor is mounted on a soft
band around the foot.

---

## 4. DS18B20 — Body temperature

| Module pin | ESP32 pin | Note |
|---|---|---|
| VDD (red) | 3V3 | Normal (non-parasitic) power mode |
| DATA (yellow) | GPIO 4 | **Requires a 4.7 kΩ pull-up to 3V3** |
| GND (black) | GND | |

```
        3V3
         │
        ┌┴┐
        │ │ 4.7 kΩ
        └┬┘
         │
GPIO 4 ──┴────────── DATA (DS18B20)
```

The pull-up is **mandatory**. Without it the OneWire bus never idles high and
`getDeviceCount()` returns 0, which the firmware reports as `DS18B20 FAIL` at
boot.

Use the waterproof stainless-steel probe variant and place it in the axilla
(armpit). Skin-contact temperature reads roughly 0.3–0.5 °C below core
temperature; the thresholds in `config.h` are set for skin contact.

---

## 5. MPU6050 — Movement and orientation

| Module pin | ESP32 pin | Note |
|---|---|---|
| VCC | 3V3 | Breakouts have an on-board 3.3 V regulator |
| GND | GND | |
| SCL | GPIO 22 | Shared bus |
| SDA | GPIO 21 | Shared bus |
| XDA / XCL | *not connected* | Auxiliary master bus, unused |
| AD0 | GND or floating | Keeps the address at `0x68` |
| INT | *not connected* | The firmware polls at 20 Hz |

Mount the module flat on the infant's chest with the **+Z axis pointing away
from the body**. The orientation logic in `MovementManager::updateOrientation()`
assumes this: +Z up means supine (`NORMAL`), and an inverted +Z means prone.

---

## 6. MAX9814 — Microphone (cry detection)

| Module pin | ESP32 pin | Note |
|---|---|---|
| VDD | 3V3 | 2.7–5.5 V input range |
| GND | GND | |
| OUT | GPIO 34 | Analog envelope, ADC1_CH6 |
| GAIN | *floating* | Floating = 60 dB. GND = 50 dB, VDD = 40 dB |
| A/R | *floating* | Attack/release ratio 1:4000 |

**Why GPIO 34?** ADC2 on the ESP32 is unusable while Wi-Fi is active, and
GPIO 34 is input-only, so it cannot be accidentally driven as an output. The
firmware configures it for 12-bit resolution with 11 dB attenuation, giving
the full 0–3.3 V range.

The MAX9814 output idles at roughly half its supply. `CryDetector::begin()`
measures that quiescent DC bias at boot and treats a reading pinned at 0 or
4095 as "module not connected".

---

## 7. Alert outputs

### Active buzzer

| Buzzer pin | ESP32 pin | Note |
|---|---|---|
| + / VCC / I/O | GPIO 25 | Active-high module: HIGH drives the tone |
| − / GND | GND | |

An **active** buzzer contains its own oscillator, so a steady logic HIGH is
enough — no PWM or `tone()` is needed. If the module draws more than ~20 mA,
drive it through an NPN transistor (e.g. 2N2222) with a 1 kΩ base resistor
rather than directly from the GPIO.

### LEDs

| LED | Anode (+) via resistor | Cathode (−) |
|---|---|---|
| Red (warning/alert) | GPIO 26, 220 Ω series | GND |
| Green (normal) | GPIO 27, 220 Ω series | GND |

```
GPIO 26 ──[220Ω]──▶|── GND     (red)
GPIO 27 ──[220Ω]──▶|── GND     (green)
```

A 220 Ω resistor at 3.3 V gives roughly 7 mA per LED, comfortably inside the
ESP32's 12 mA per-pin recommended limit.

---

## 8. Power architecture

```
   18650 Li-ion cell (3.0 - 4.2 V)
            │
            ▼
   ┌──────────────────┐
   │  TP4056 module   │   charging + over-discharge / over-current protection
   │  (with DW01A)    │   micro-USB 5 V in
   └────────┬─────────┘
            │  B+ / OUT+  (3.0 - 4.2 V)
            ▼
   ┌──────────────────┐
   │  ON / OFF switch │   SPST, in series with the positive rail
   └────────┬─────────┘
            ▼
   ┌──────────────────┐
   │ MT3608 boost     │   trim the potentiometer to 5.0 V BEFORE connecting
   │ converter        │   the ESP32
   └────────┬─────────┘
            │  5 V
            ▼
   ┌──────────────────┐
   │  ESP32 DevKit    │   fed through the micro-USB / VIN 5 V input
   │  on-board LDO    │   on-board AMS1117 derives the 3.3 V rail
   └────────┬─────────┘
            │  3V3
            ▼
   All sensor modules + OLED
```

| Stage | Component | Purpose |
|---|---|---|
| Cell | 18650 Li-ion | ~2000–3000 mAh at a 3.7 V nominal |
| Charge / protect | TP4056 + DW01A | CC/CV charging, over-discharge and over-current cut-off |
| Isolation | SPST switch | Breaks the positive rail between TP4056 OUT+ and MT3608 IN+ |
| Boost | MT3608 | Steps 3.0–4.2 V up to a regulated 5 V |
| Regulate | ESP32 on-board AMS1117 | Derives the 3.3 V sensor rail from 5 V |

**Set the MT3608 output before connecting anything.** Power the module from the
battery, measure OUT+ with a multimeter, and turn the multi-turn potentiometer
until it reads 5.0 V. The MT3608 can output up to 28 V, which would instantly
destroy the ESP32.

**Grounds must be common.** The battery negative, TP4056 ground, MT3608 ground
and ESP32 GND all tie to a single ground node.

> **No battery monitoring in firmware.** There is no voltage divider from the
> cell into an ADC pin in this design, so the firmware cannot know the battery
> level and does not pretend to. Adding one is listed as a future enhancement
> in the README.

---

## 9. Bring-up checklist

1. **Power first, alone.** Set the MT3608 to 5.0 V with nothing else attached.
2. **Scan the I2C bus.** Flash a bare `i2c_scanner` sketch. Expect exactly
   `0x3C`, `0x57` and `0x68`. A missing address means a wiring fault; an
   unexpected one means an address conflict.
3. **Check the OneWire pull-up.** With the DS18B20 connected and idle, GPIO 4
   should measure ~3.3 V.
4. **Check the microphone bias.** With the room quiet, GPIO 34 should sit near
   half of the module's supply. A reading of 0 V or 3.3 V means it is not
   wired correctly.
5. **Flash the firmware.** The boot screen reports OK/FAIL per component; a
   FAIL line points directly at the subsystem to re-check.

---

## 10. Known hardware limitations

- All three I2C devices share one bus, so a single shorted SDA/SCL line takes
  down vitals, movement and display together.
- The MAX30102 is highly sensitive to motion artefacts and ambient light.
- There is no patient electrical isolation of any kind.
- There is no watchdog-backed hardware failsafe: if the firmware hangs, the
  buzzer and LEDs freeze in whatever state they were last written.
