# Friday 🐶

**Friday is a cute, chatty desk robot with big puppy eyes and wiggly ears.** You can talk with
her, ask her for stories, and learn things together. She reacts to how you treat her: pick her
up and she gets excited, shake her and she gets dizzy, lay her face-down and she takes a nap.

She runs on a [DFRobot UNIHIKER K10](https://www.unihiker.com/products/k10) (ESP32-S3 with a
screen, microphones, speaker and accelerometer) plus two micro servos for the ears. The firmware
is built on the excellent open-source
[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) voice assistant.

## Features

- 🗣️ **Voice conversations** through xiaozhi's AI backend (wake word, streaming speech, LLM)
- 👀 **Animated puppy face**: glossy eyes, blinking, glancing around, happy squints, eyebrows,
  blush, and an ω mouth that moves while she talks. Over 20 expressions.
- 👂 **Servo ears** that follow her mood: perk up to listen, wiggle while talking, droop when sad.
  They hold still while listening so servo noise stays out of the microphones.
- 🤸 **Motion reactions**: picked up, put down, shaken, upside down, tilted, on her back, face-down
- 💤 **Dozes off** when nobody talks to her for a while
- 🛠️ **MCP tools**, so the AI can wiggle her ears, pull faces and check her posture by itself

## Repository layout

| Path | What's there |
|---|---|
| [`main/boards/friday/friday-k10/`](main/boards/friday/friday-k10/) | Friday's firmware: face, ears, motion, personality ([README](main/boards/friday/friday-k10/README.md)) |
| [`3dmodels/`](3dmodels/) | Printable enclosure: base, cover, ear |
| [`friday.ps1`](friday.ps1) | Windows helper to build / flash / monitor |
| everything else | Upstream xiaozhi-esp32 |

## Hardware

- DFRobot UNIHIKER K10
- 2 × micro servo (SG90 or similar; quieter "digital" servos are better)
- 5 V supply for the servos, plus a 470–1000 µF capacitor
- 3D-printed shell from [`3dmodels/`](3dmodels/)

| Servo | K10 edge pin |
|---|---|
| Left ear signal | P0 (GPIO1) |
| Right ear signal | P1 (GPIO2) |
| V+ / GND | external 5 V, GND shared with the K10 |

## Build & flash

Requires **ESP-IDF v6.1** (v6.0.1 minimum).

```powershell
# First build (applies the board's sdkconfig settings)
python scripts/build.py friday/friday-k10 --language en-US

# Then, on Windows, using the helper
powershell -ExecutionPolicy Bypass -File .\friday.ps1 build
powershell -ExecutionPolicy Bypass -File .\friday.ps1 flash COM7
```

On other platforms, activate ESP-IDF and run `idf.py -p <port> flash monitor` from the repo root.

On first boot, join the `Xiaozhi-XXXX` Wi-Fi hotspot to configure Wi-Fi, then enter the
activation code Friday speaks at [xiaozhi.me](https://xiaozhi.me). A ready-made personality
prompt is in the [board README](main/boards/friday/friday-k10/README.md#personality-prompt-paste-into-the-xiaozhime-agent-role-settings).

## Staying in sync with xiaozhi

This repo is a fork of xiaozhi-esp32. To pull in upstream improvements:

```bash
git remote add upstream https://github.com/78/xiaozhi-esp32.git   # once
git config merge.ours.driver true                                 # once: keeps this README on merges
git fetch upstream
git merge upstream/main
```

Friday's code lives in its own board folder. Only `main/CMakeLists.txt` and
`main/Kconfig.projbuild` carry small edits to register it, so merge conflicts should be rare.

## Credits & license

Built on [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) by Shenzhen Xinzhi Future
Technology Co., Ltd. and contributors. See the
[upstream README](https://github.com/78/xiaozhi-esp32#readme) for the full xiaozhi docs.
MIT licensed, see [LICENSE](LICENSE).
