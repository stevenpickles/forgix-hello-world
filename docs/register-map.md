# Runtime register map

Transactions keep chip select low throughout. Bytes are MSB-first; the FPGA
samples SDIO on rising SCK edges. For reads, the MCU releases SDIO while the
last request clock is high and the FPGA starts driving after its falling edge.

| Command | Value | Transaction |
| --- | ---: | --- |
| Write | `0x02` | `02 addr data` |
| Read | `0x03` | `03 addr`, then one byte from FPGA |
| Reset | `0x7f` | Restore defaults |
| Ping | `0x9f` | Return design ID `0xb6` |

| Address | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | ID | R | `0xb6` |
| `0x01` | STATUS | R/W | ready, debounced button, event, SPI active, raw button, SPI error |
| `0x02` | FEATURES | R | bit 0 LED, bit 1 button |
| `0x10..0x12` | LED R/G/B | R/W | PWM intensity |
| `0x13` | LED GLOBAL | R/W | global brightness |
| `0x14` | LED ENABLE | R/W | bit 0 enables output |
| `0x20` | BUTTON LEVEL | R | debounced and synchronized raw state |
| `0x21` | BUTTON COUNT | R/W | saturating press count; write zero to clear |
| `0x30` | TICK CAPTURE | W | any written value latches the free-running counter into TICK 0..3 |
| `0x30..0x33` | TICK 0..3 | R | latched counter snapshot, `0x30` bits 7:0 up to `0x33` bits 31:24 |

Reset defaults are dim blue: RGB `0, 0, 0x20`, global brightness `0x40`, enabled.

The tick counter runs at the 32 MHz fabric clock and wraps every ~134 s. It is
cleared only by power-on reset, not by the Reset command: it is a timebase, not
a register with a default. Capture before reading -- the four bytes describe the
single instant of the last capture write, so they cannot tear across the four
read transactions.

