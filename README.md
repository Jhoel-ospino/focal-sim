# 🖥️ Focal Sim — Desktop CNC Simulator & Test Suite

> **Native Desktop Simulation Environment & Automated Testing Suite (SDL2 + LVGL v8) for Focal CNC Laser Cutter.**

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC_BY--NC--SA_4.0-lightgrey.svg)](LICENSE)
[![SDL2](https://img.shields.io/badge/SDL-2.0-blue.svg)](https://www.libsdl.org/)
[![Target: LVGL](https://img.shields.io/badge/Target-LVGL_v8.3-orange.svg)](https://lvgl.io/)
[![C11](https://img.shields.io/badge/C-11-blue.svg)]()
[![Tests](https://img.shields.io/badge/Tests-29%2F29_Passing-brightgreen.svg)]()
[![Author: Jhoel Ospino](https://img.shields.io/badge/Author-Jhoel_Ospino-purple.svg)](https://github.com/Jhoel-ospino)

---

## 📖 Overview

**Focal Sim** is a native cross-platform simulation and regression testing harness for the **Focal CNC Laser Cutter**. It runs on macOS and Linux (x86_64 and Apple Silicon ARM64), providing an exact digital twin of the embedded system without requiring physical hardware:

- **1:1 Hardware Virtualization**: Mocks FreeRTOS tasks, ESP32-S3 logging (`esp_log.h`), NVS flash settings (`nvs_flash.h`), Wi-Fi station scan/connect, 20W PWM optical laser modulation, and GRBL 1.1 motion queues.
- **Interactive Touch GUI**: Renders the complete 480×320 TFT touchscreen interface with mouse-as-touch simulation via SDL2 at 60 FPS.
- **Headless Adversarial Test Suite**: Executes 29 automated test cases covering state transitions, navigation loops, rapid jog clamping, G-code preview parsing, material presets, and memory integrity.

---

## 🏗️ Architecture

```
                  ┌──────────────────────────────────────────┐
                  │          Focal Sim (SDL2 Window)         │
                  │   480x320 Canvas  •  60 FPS Event Loop   │
                  └─────────────────────┬────────────────────┘
                                        │
                    ┌───────────────────┴───────────────────┐
                    ▼                                       ▼
        ┌───────────────────────┐               ┌───────────────────────┐
        │   LVGL v8.3 Engine    │               │  Mock System Services │
        │ • 7 UI Screens        │               │ • Wi-Fi AP / STA Mock │
        │ • Liquid Wave FX      │               │ • NVS Settings Store  │
        │ • Vector Focal Icons  │               │ • PWM Laser State     │
        │ • G-Code Canvas       │               │ • GRBL 1.1 Parser     │
        └───────────────────────┘               └───────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites
On **macOS** (Homebrew):
```bash
brew install cmake sdl2
```

On **Ubuntu / Debian**:
```bash
sudo apt-get update && sudo apt-get install -y cmake libsdl2-dev build-essential
```

---

### Building the Simulator
```bash
git clone https://github.com/Jhoel-ospino/focal-sim.git
cd focal-sim
cmake -B build
cmake --build build -j
```
*(CMake will automatically download and configure LVGL v8 via FetchContent if not already present).*

---

### Running Interactive Mode
```bash
./build/focal_sim
```
- Click with mouse to simulate physical resistive touch.
- Real-time DRO updates, Jogging pad, Material database, and Wi-Fi manager are fully functional.

---

### Running Automated Test Suite
To execute the automated regression test battery:
```bash
# Run all 29 regression tests
./build/focal_sim --test-all

# Run specific screen test
./build/focal_sim --test-screen jog
./build/focal_sim --test-screen monitor
./build/focal_sim --test-screen calibration
```

---

## ⚖️ License & Intellectual Property

**Copyright (c) 2026 Jhoel Ospino. All rights reserved.**

This project is licensed under the **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0)**.

- **Non-Commercial Only**: Commercial exploitation, distribution, or inclusion in paid products/hardware without prior written permission from the copyright owner (Jhoel Ospino) is strictly prohibited.
- **Community & Education**: You are free to study, modify, and build upon this project for open community and non-commercial educational purposes.
- **Attribution**: Credit to **Jhoel Ospino** must remain intact in all derivative works.
