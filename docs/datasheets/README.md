# Datasheets

## APS1604M-3SQR-QSPI-PSRAM.pdf

AP Memory APS1604M-3SQR, rev 3.1, Nov 2023. The QSPI/QPI PSRAM on QMI chip
select 1, chip select on GPIO 0.

Key facts for the driver:

- `VDD 2.7-3.6 V`, 16 Mb organised 2M x 8, `A[20:0]` -- 2 MByte.
- 133 MHz wrapped burst at 3.0 V, 109 MHz at 3.3 V, 84 MHz linear-512.
- **Read ID (`9Fh`) is limited to 33 MHz.** It takes zero wait cycles, so unlike
  the 133 MHz burst commands nothing covers the device's output-valid time. The
  plain Read (`03h`) shares the limit for the same reason. Exceeding it samples
  before the data is valid and returns displaced bytes.
- Read ID may only be issued straight after a Global Reset plus `tRST` 50 ns.
- Expected response: vendor `0Dh` at byte 4, known-good-die `5Dh` at byte 5, then
  EID whose top three bits encode density -- `000` for this 2 MByte part.
- The fitted device answers KGD `0x0B`, EID `0x43` instead; see
  [the built-in test reference](../ibit.md#what-it-reports-but-does-not-judge)
  for the open question.
- Command widths matter. `F5h` Exit Quad Mode is a **quad** command and does not
  exist in serial mode; `66h`/`99h` reset exist in both. A device left in QPI
  cannot be reset by serial-width commands.
- `tCEM` bounds how long chip select may stay asserted, since the part cannot
  self-refresh while selected.
