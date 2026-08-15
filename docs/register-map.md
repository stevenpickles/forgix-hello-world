# Runtime register map

Transactions keep chip select low throughout. Bytes are MSB-first; the FPGA
samples SDIO on rising SCK edges. For reads, the MCU releases SDIO while the
last request clock is high and the FPGA starts driving after its falling edge.

| Command | Value | Transaction |
| --- | ---: | --- |
| Write | `0x02` | `02 addr data` |
| Read | `0x03` | `03 addr`, then one byte from FPGA |
| Reset | `0x7f` | Restore defaults |
| Ping | `0x9f` | Return design ID `0xb8` |

| Address | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | ID | R | `0xb8` |
| `0x01` | STATUS | R/W | read: bit 0 ready (always 1), bit 1 debounced button, bit 2 button event (sticky), bit 4 raw button, bit 7 SPI error (sticky); bits 3, 5, 6 reserved, read 0. Write: bit 2 set acknowledges the button event (write-one-to-clear; zeros clear nothing, and a press on the acknowledging clock edge wins -- the event stays set); other write bits are ignored |
| `0x02` | FEATURES | R | bit 0 LED, bit 1 button, bit 2 GPO0 |
| `0x10..0x12` | LED R/G/B | R/W | PWM intensity |
| `0x13` | LED GLOBAL | R/W | global brightness |
| `0x14` | LED ENABLE | R/W | bit 0 enables output |
| `0x15` | GPO0 | R/W | bit 0 drives board-edge FPGA `PIN13` / Trion ball `F5`; upper bits read zero |
| `0x20` | BUTTON LEVEL | R | debounced and synchronized raw state |
| `0x21` | BUTTON COUNT | R/W | saturating press count; only a write of zero does anything, and it clears the button event too. A press landing on the clearing clock edge survives: the count reads 1 and the event stays set |
| `0x30` | TICK CAPTURE | W | any written value latches the free-running counter into TICK 0..3 |
| `0x30..0x33` | TICK 0..3 | R | latched counter snapshot, `0x30` bits 7:0 up to `0x33` bits 31:24 |

Reset defaults are dim blue: RGB `0, 0, 0x20`, global brightness `0x40`, enabled,
and GPO0 low. FPGA `PIN13` is distinct from RP2354 GPIO13/UART0 RX.

The tick counter runs at the 32 MHz fabric clock and wraps every ~134 s. It is
cleared only by power-on reset, not by the Reset command: it is a timebase, not
a register with a default. Capture before reading -- the four bytes describe the
single instant of the last capture write, so they cannot tear across the four
read transactions.
