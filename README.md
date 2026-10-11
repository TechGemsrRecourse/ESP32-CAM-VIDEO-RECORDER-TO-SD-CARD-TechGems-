# ESP32-CAM One-Button Video Recorder

A simple, single-file Arduino sketch that turns an AI-Thinker ESP32-CAM into a standalone video recorder controlled by one push-button. Press once to start recording to the SD card, press again to stop. No Wi-Fi, no web server, no extra libraries or tabs.

## Features

- **One button control:** press to start, press again to stop
- **Idle on boot:** the camera never records automatically when power is applied
- **VGA (640x480) Motion-JPEG** saved as standard `.avi` files (plays in VLC and most players)
- **SD card in 1-bit mode** (frees up GPIO 4, 12 and 13)
- **Status LED:** the small red LED on the back of the board (GPIO 33) is on while recording
- **Safe file closing:** the AVI index and header are finalized on stop, so files are not corrupted
- **Auto-numbered files:** `video_0001.avi`, `video_0002.avi`, and so on
- **Correct playback speed:** the real frame rate is measured and written into the file
- **Safety limit:** recording stops automatically at 30,000 frames and the file is closed properly
- **Error indicator:** a fast-blinking red LED signals an SD card or camera failure (details printed to Serial)
- **Single file:** copy and paste into a blank Arduino sketch

## Hardware

| Part | Notes |
|---|---|
| ESP32-CAM (AI-Thinker) | With PSRAM |
| USB programming shield / daughterboard | No jumper wires needed for flashing |
| Momentary push-button | 2 or 4 leg type |
| MicroSD card | FAT32, 32 GB or smaller recommended, Class 10 |

### Wiring

| Button leg | Board pin |
|---|---|
| One side | **GPIO 12 (D12)** |
| Other side | **GND** |

No external resistor is needed. The sketch enables the internal pull-up on GPIO 12.

> **Note for 4-leg buttons:** legs on the same side are connected internally. Wire two legs that are diagonal from each other.

> **Note:** GPIO 12 is a boot-strapping pin. A button to GND is safe, but do not pull it HIGH.

## Software setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add the ESP32 board package (**esp32 by Espressif Systems**) via Boards Manager.
3. Open a new blank sketch and paste in the contents of the `.ino` file from this repository.
4. Select these settings:
   - **Board:** AI Thinker ESP32-CAM
   - **PSRAM:** Enabled (if the option is shown)
   - **Upload speed:** 115200
5. Upload the sketch.
6. Open the Serial Monitor at **115200 baud**.

## Usage

1. Insert a FAT32-formatted microSD card.
2. Power the board. The Serial Monitor shows `Ready. Press the button to start/stop recording.`
3. **Press the button:** the red LED turns on and recording begins.
4. **Press the button again:** recording stops, the file is closed, and the LED turns off.
5. **Wait for the LED to turn off before removing the card.**
6. Open the `.avi` file on your computer.

## Configuration

These values are near the top of the sketch:

| Setting | Default | Description |
|---|---|---|
| `JPEG_QUALITY` | `12` | Lower number means better quality and larger files. Try `15` if you see dropped frames. |
| `MAX_FRAMES` | `30000` | Automatic stop limit |
| `FRAME_W` / `FRAME_H` | `640` / `480` | VGA resolution |

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Button does nothing | Check wiring. Test with a jumper between GPIO 12 and GND. |
| Fast-blinking red LED | Error. Check the Serial Monitor: SD mount failed, no card, or camera init failed. |
| SD card mount failed | Card not FAT32 (cards over 32 GB are often exFAT), or not seated properly |
| Camera init failed | Ribbon cable not fully seated, or PSRAM not enabled |
| Board keeps rebooting | Weak USB power. Try a different cable or port. |
| Low frame rate or dropped frames | Use a faster card or raise `JPEG_QUALITY` to `15` |
| Upload fails | Disconnect the button from GPIO 12 during upload and confirm the board selection |

## Notes and limitations

- Video only, no audio.
- Do not cut power while recording. The AVI index is written on stop, so an interrupted file may need repair in VLC.
- Typical file size is roughly 1 to 2 MB per second at VGA.
- The camera and SD card are initialized on the start press and shut down on the stop press.

## Ideas for improvement

- Timestamps in filenames using an RTC or NTP
- Configurable recording time limit
- Blinking LED while recording
- Audio via an I2S microphone

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgements

Code developed with AI assistance and tested on AI-Thinker ESP32-CAM hardware.
