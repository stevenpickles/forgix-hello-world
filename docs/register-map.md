# Runtime register map

Transactions keep chip select low throughout. Bytes are MSB-first; the FPGA
samples SDIO on rising SCK edges and changes read data on falling edges.

| Command | Value | Transaction |
| --- | ---: | --- |
| Write | `0x02` | `02 addr data` |
| Read | `0x03` | `03 addr`, then one byte from FPGA |
| Reset | `0x7f` | Restore defaults |
| Ping | `0x9f` | Return design ID `0xb5` |

| Address | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | ID | R | `0xb5` |
| `0x01` | STATUS | R/W | ready, debounced button, event, SPI active, raw button, SPI error |
| `0x02` | FEATURES | R | bit 0 LED, bit 1 button |
| `0x10..0x12` | LED R/G/B | R/W | PWM intensity |
| `0x13` | LED GLOBAL | R/W | global brightness |
| `0x14` | LED ENABLE | R/W | bit 0 enables output |
| `0x20` | BUTTON LEVEL | R | debounced and synchronized raw state |
| `0x21` | BUTTON COUNT | R/W | saturating press count; write zero to clear |

Reset defaults are dim blue: RGB `0, 0, 0x20`, global brightness `0x40`, enabled.

