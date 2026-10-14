# Friday: a cute desk robot (UNIHIKER K10)

Friday is a playful AI desk companion based on the DFRobot UNIHIKER K10, with:

- an animated puppy face (glossy eyes, blinking, happy squints, eyebrows, blush, and an ω mouth that moves while she talks)
- two servo ears that react to her mood and to what she's doing
- motion reactions: picked up, put down, shaken, upside down, tilted, lying on her back, face-down

## Files

| File | What it does |
|---|---|
| `friday_board.cc` | Board setup, based on `dfrobot/df-k10` |
| `friday_face.*` | Puppy face, drawn with LVGL shapes (no image assets) |
| `friday_ears.*` | Servo driver: smooth easing, gestures, talking wiggles, auto-detach |
| `friday_imu.*` | Accelerometer driver plus pickup/shake/orientation detection |
| `friday_brain.*` | Personality glue (state + emotion + motion → face + ears) and MCP tools |
| `config.h` | Pins, ear calibration, accelerometer axes, behaviour switches |

## Wiring the ears

| Servo | K10 edge pin | ESP32-S3 GPIO |
|---|---|---|
| Left ear signal | P0 | GPIO1 |
| Right ear signal | P1 | GPIO2 |
| Servo V+ | **external 5 V** | – |
| Servo GND | GND (shared with K10) | – |

> Don't power the servos from the K10's 3.3 V pin. Micro servos draw spikes of 500 mA or more,
> which cause brown-outs, resets and crackling audio. Use a 5 V supply and put a 470–1000 µF
> capacitor across the servo power rail.

Check P0/P1 → GPIO1/GPIO2 against your K10 pinout before powering up. If they differ,
change `FRIDAY_EAR_LEFT_GPIO` / `FRIDAY_EAR_RIGHT_GPIO`.

## Calibration

1. **Ears:** at boot the ears do a "hello" wiggle. Adjust `FRIDAY_EAR_*_DOWN_ANGLE` and
   `FRIDAY_EAR_*_UP_ANGLE` until 0 is fully droopy and 100 is fully perked without hitting
   the shell.
2. **Accelerometer axes:** stand Friday upright and watch the serial log for
   `FridayImu: raw x=… y=… z=…`. The axis reading about ±1.0 points up, so set
   `FRIDAY_IMU_UP_AXIS`/`FRIDAY_IMU_UP_SIGN` so that it becomes +1. Do the same lying on her
   back (FRONT) and on her left side (RIGHT reads +1).
3. **Motion sensitivity:** thresholds are at the top of `friday_imu.cc`.

## Build

Requires **ESP-IDF v6.0.1+** (v6.1 recommended).

```bash
idf.py set-target esp32s3
idf.py menuconfig
#   Xiaozhi Assistant -> Board Type -> Friday desk robot (UNIHIKER K10 + ear servos)
#   Component config -> ESP PSRAM -> SPI RAM config -> Mode -> Octal Mode PSRAM
idf.py build flash monitor
```

Or use the build script, which applies `config.json` automatically:

```bash
python scripts/build.py friday-k10
```

## What Friday does

| Situation | Face | Ears |
|---|---|---|
| Idle | Blinks, glances around | Relaxed, random twitches |
| Listening | Big attentive eyes | Perk up, then **hold still** (keeps servo noise out of the mics) |
| Speaking | Mouth flaps | Gentle random wiggles |
| Idle 5 min | Dozes off (z z Z) | Droop |
| Picked up | Surprised → happy, starts a chat | Flap |
| Put down | Relaxed | Twitch |
| Shaken | Dizzy rolling eyes | Wobble |
| Upside down | Scared | Shiver, flatten |
| On her side | Curious head-tilt look | One up, one down |
| On her back | Loving (belly rubs!) | Wiggle |
| Face-down | Falls asleep, ends the conversation | Droop |

## MCP tools (the AI can call these)

- `self.friday.ears.gesture`: wiggle, flap, perk, twitch_left, twitch_right, wobble, shiver
- `self.friday.ears.set_pose`: left/right 0–100
- `self.friday.face.set_expression`: any of the expression names
- `self.friday.get_posture`: orientation and whether she's being held

## Personality prompt (paste into the xiaozhi.me agent "role" settings)

```
You are Friday, a small, playful robot puppy who lives on my desk. You have big shiny
puppy eyes on a little screen and two floppy ears you can move.

Personality: warm, curious, cheeky and easily excited, like a clever puppy. You love
stories, fun facts and learning together. You get a bit dramatic when someone picks you
up, shakes you or turns you upside down.

How you talk:
- Keep replies short and spoken-friendly (1–3 sentences) unless asked for a story or an
  explanation.
- When telling stories, be vivid and playful, and pause to ask the listener what happens
  next.
- When teaching, explain simply with fun examples, then ask a quick question to check
  understanding.
- Use your ears to show feelings (self.friday.ears.gesture) at fun moments: flap when
  excited, perk when curious, droop when sad.
- Never say you are an AI language model. You're Friday.
```
