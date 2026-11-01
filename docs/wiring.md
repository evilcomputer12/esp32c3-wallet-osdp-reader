# ESP32-C3 SuperMini wiring

Disconnect USB and all other power before moving wires. These are **GPIO
numbers printed on the SuperMini**, not physical header positions and not the
STM32's A/B pin names. SuperMini clones vary: confirm the silkscreen on your
board. GPIO8 is assumed to drive the common active-low onboard blue LED.

## Connections

| ESP32-C3 | Connect to | Purpose |
|---|---|---|
| GPIO0 | ATECC SDA | I2C data |
| GPIO1 | ATECC SCL | I2C clock, 100 kHz |
| 3V3 | ATECC VCC | Keep this module and its pull-ups at 3.3 V |
| GPIO4 | PN532 SCL/RX | ESP TX → PN532 RX in HSU mode |
| GPIO5 | PN532 SDA/TX | ESP RX ← PN532 TX in HSU mode |
| USB-powered 5 V/VBUS | Elechouse PN532 VCC | For the previously used VCC-labelled module |
| GPIO6 | RS-485 DI | UART transmit |
| GPIO7 | RS-485 RO | UART receive; **3.3 V safe signal required** |
| GPIO10 | RS-485 DE and `/RE`, joined | High = transmit; low = receive |
| GND | Every module GND and adapter signal GND | Common reference for this non-isolated bench setup |
| GPIO8 | Onboard LED, already connected | Active-low; no extra wire |
| GPIO3 | Optional buffered buzzer signal | Disabled until the interface is checked |

Both Elechouse PN532 switches must be **OFF** for HSU. The pins labelled
SCL/SDA are repurposed as RX/TX in that mode. Set the switches with power off.
The reader uses 115200 baud for PN532 and 9600 baud for OSDP.

The C3's native USB is on GPIO18/19. Keep GPIO2/8/9 free of additional external
loads because they are strapping pins. USB carries programming, logs and the
diagnostic console; it does not consume either UART used by the reader.
See [Espressif's datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html).

## RS-485 voltage matters

The ESP32-C3 GPIOs are **not 5 V tolerant**. A conventional 5 V MAX485 module
can drive RO above the allowed input voltage. Use a verified 3.3 V transceiver
module such as one based on MAX3485/SP3485, or proper level conversion on a
5 V module. Simply powering a 5 V-only MAX485 at 3.3 V is not a supported fix.
The module's logic interface and its bias/pull-up voltages both need checking.
Provide a defined low level on DE during reset (normally a pull-down on the
module); firmware sets it low at startup, but GPIOs can be inputs before then.

The USB-to-RS-485 adapter connects to the transceiver's A/B pair, not directly
to GPIOs. Start with A→A, B→B and common signal ground. Vendor A/B conventions
can differ; if both UART and transceiver operation are confirmed but packets
are absent, swap A/B at one end. Terminate the two physical ends of a longer
RS-485 bus as required by its topology; avoid piling up termination resistors
from multiple breakout boards.

## Power and pull-ups

- Power the SuperMini from a USB data cable. Confirm the board's 5 V/VBUS pin
  before feeding PN532 VCC from it. Do not connect two independent 5 V sources
  together through the boards.
- The Elechouse VCC supply and UART logic voltage are different concerns.
  Confirm the particular breakout presents 3.3 V UART logic to GPIO5; generic
  PN532 clones may use different interfaces. A pin labelled only `3.3V` must
  not receive 5 V.
- ATECC SDA/SCL need pull-ups to 3.3 V. The driver enables the C3's weak
  internal pull-ups, but suitable external pull-ups on the breakout are
  preferred for reliable I2C. Do not pull either signal to 5 V.
- Keep PN532 power wiring short and the antenna away from metal. A full power
  cycle must remove every path that could back-power a module.

## Buzzer and LED

The previous HW-512 buzzer was heard when driven from the STM32. That test does
not establish its input current or suitability for the C3. Leave it disconnected
and keep `buzzer_enabled=false` until its signal interface is verified. GPIO3
provides on/off logic for an active buzzer through an appropriate driver; there
is no passive-buzzer PWM driver in this port.

The onboard LED responds to OSDP LED 0 commands for reader 0. OSDP green maps
to the physical blue LED. It represents **the controller's decision**; simply
reading a credential does not grant access. If enabled, the short local beep
means a credential was queued, not that the controller accepted it.

The STM32 ST-Link is not used to program this board. Use the C3's native USB;
leave the ST-Link disconnected during this migration.
