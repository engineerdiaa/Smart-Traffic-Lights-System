# Smart Traffic Lights System

An Arduino-based automatic traffic intersection controller featuring synchronized vehicle and pedestrian traffic lights, pedestrian request buttons, adjustable timing, red-light violation detection, an LCD status display, and a maintenance mode.

This project was designed as an educational embedded-systems project demonstrating how a real traffic intersection can be modeled using a non-blocking finite state machine.

## Features

- Fully automatic two-direction traffic light cycle
- Synchronized vehicle and pedestrian signals
- 10-phase finite state machine
- Pedestrian crossing request buttons
- Hardware interrupts for pedestrian buttons
- Automatic shortening of the active traffic phase when a pedestrian request is waiting
- Adjustable green-light duration using a potentiometer
- Red-light violation detection using LDR sensors
- Camera-style double flash when a violation is detected
- Buzzer feedback
- 16x2 I2C LCD system status display
- Maintenance mode with flashing yellow traffic lights
- Non-blocking timing using `millis()`
- ADC cross-talk handling for the LDR sensors

## Project Files

The repository contains two implementations of the same traffic control system.

### `AutomaticTrafficLights_Readable.ino`

The recommended version for learning, modifying, and understanding the project.

It uses descriptive variable names, helper functions, enums, structures, and an explicit state machine to make the program easier to follow.

Use this version if you want to:

- Study how the system works
- Modify the traffic sequence
- Add new sensors or features
- Debug the system
- Use the project for learning embedded programming

### `Automatic_Optimized.ino`

A more compact implementation of the same system.

This version replaces much of the repeated traffic-light control logic with:

- Bitmasks
- Data-driven state definitions
- Compact pin arrays
- Reduced repetitive code
- State-based traffic color inference

For example, the state of the traffic and pedestrian LEDs is stored using binary bitmasks instead of repeatedly calling individual light-control functions.

This makes the implementation considerably more compact, but also less beginner-friendly.

## How the System Works

The intersection contains two traffic directions:

- **Traffic A**
- **Traffic B**

Each direction has a standard:

- Red light
- Yellow light
- Green light

There are also two pedestrian crossings:

- **Pedestrian A**
- **Pedestrian B**

The controller moves continuously through a 10-state traffic sequence.

Only compatible traffic and pedestrian movements are allowed at the same time, preventing conflicting green signals.

### Pedestrian Requests

Each pedestrian crossing has a push button.

The buttons are connected to Arduino interrupt-capable pins so pedestrian requests can be detected immediately without constantly polling the buttons.

When a valid pedestrian request is received, the controller stores the request and can shorten the current green phase so that the pedestrian crossing is served sooner.

The request is then cleared once the appropriate pedestrian phase begins.

## Adjustable Traffic Timing

A potentiometer connected to `A0` controls the duration of the main green-light phases.

The green duration is mapped approximately between:

```text
5 seconds -> 12 seconds
```

The LCD temporarily displays the newly selected timing whenever the potentiometer is adjusted.

## Red-Light Violation Detection

Two LDR sensors monitor the traffic lanes.

The sensors are used to detect a vehicle/object crossing the monitored point while its corresponding traffic signal is red.

The detection system uses separate dark and light thresholds together with a confirmation delay to reduce false detections.

When a violation is confirmed:

1. The camera flash is triggered.
2. A short pre-flash occurs.
3. A short gap follows.
4. The main flash fires.
5. The buzzer provides feedback.

A cooldown prevents the same sensor from immediately triggering repeated flashes.

## Maintenance Mode

Moving the potentiometer close to its minimum position activates maintenance mode.

In maintenance mode:

- Normal traffic sequencing stops
- Both traffic yellow lights blink
- Pedestrian signals remain red
- Pedestrian requests are cleared
- Red-light detection is disabled
- The buzzer pulses periodically
- The LCD displays:

```text
SYSTEM OFFLINE
*MAINTENANCE ON*
```

This simulates the flashing-yellow behavior commonly used when a traffic signal controller is offline or undergoing maintenance.

## LCD Display

The project uses a 16x2 I2C LCD at address:

```text
0x27
```

During normal operation, it displays the state of Traffic A and Traffic B using:

```text
GO
YEL
STP
```

The second row displays pedestrian status such as:

```text
PED: AUTO CYCLE
PED: BTN PRESSED
PED: BLINKING
```

## Pin Configuration

| Component | Arduino Pin |
|---|---:|
| Potentiometer | A0 |
| Traffic A LDR | A1 |
| Traffic B LDR | A2 |
| Camera Flash LED | A3 |
| Pedestrian Button A | D2 |
| Pedestrian Button B | D3 |
| Traffic A Red | D4 |
| Traffic A Yellow | D5 |
| Traffic A Green | D6 |
| Traffic B Red | D7 |
| Traffic B Yellow | D8 |
| Traffic B Green | D9 |
| Pedestrian A Red | D10 |
| Pedestrian A Green | D11 |
| Pedestrian B Red | D12 |
| Pedestrian B Green | D13 |
| Buzzer | D1 |

The LCD communicates through I2C.

> **Note:** Pin D1 is normally the Arduino hardware serial TX pin. Using it for the buzzer may interfere with Serial communication on boards such as the Arduino Uno or Nano.

## Required Hardware

The project can be built using components such as:

- Arduino Uno/Nano or compatible board
- 6 LEDs for the two vehicle traffic lights
- 4 LEDs for the two pedestrian traffic lights
- Current-limiting resistors for the LEDs
- 2 push buttons
- 2 LDR photoresistors
- LDR voltage-divider resistors
- Potentiometer
- 16x2 I2C LCD
- Buzzer
- High-brightness LED for the simulated camera flash
- Breadboard or PCB
- Jumper wires
- Suitable power supply

## Required Libraries

The project uses:

```cpp
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
```

`Wire` is included with the Arduino framework.

You may need to install a compatible `LiquidCrystal_I2C` library through the Arduino IDE Library Manager.

## Uploading the Code

1. Clone or download this repository.
2. Open either `.ino` file in the Arduino IDE.
3. Install the required LCD library if necessary.
4. Select your Arduino board.
5. Select the correct COM/serial port.
6. Compile the sketch.
7. Upload it to the board.

For most users, start with:

```text
AutomaticTrafficLights_Readable.ino
```

## Educational Purpose

This project can be useful for students learning concepts including:

- Embedded systems
- Arduino programming
- Finite state machines
- Interrupts
- Non-blocking programming
- Sensor interfacing
- Analog signal processing
- Bitwise operations
- Hardware/software integration
- Traffic control logic

The readable implementation is intentionally kept alongside the optimized implementation so students can compare maintainable code with a more compact data-driven approach.

## Credit

**Making content with this?** Videos, posts or tutorials: tag [@diaawastaken](https://www.instagram.com/diaawastaken/).

## License

This project is licensed under the MIT License.

See the [LICENSE](LICENSE) file for details.
