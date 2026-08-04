-- The wire protocol shared by the FPGA design and the MCU that talks to it. Every
-- constant here has a counterpart in firmware/src/bsp/bsp_fpga.c, and the two copies
-- are only kept in step by hand -- nothing in either build checks them against each
-- other. Changing a value here without changing it there leaves a design that
-- configures, reports CDONE, and then answers every transaction wrongly.
--
-- DESIGN_ID is the byte a CMD_PING returns. It is the only evidence the MCU has that
-- the bitstream it just loaded is this design rather than a stale or corrupt one, so
-- it must change whenever the register map changes meaning.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;

package forgix_pkg is

  subtype byte_t is unsigned(7 downto 0);

  constant DESIGN_ID        : byte_t := x"B6";
  constant CMD_WRITE        : byte_t := x"02";
  constant CMD_READ         : byte_t := x"03";
  constant CMD_RESET        : byte_t := x"7F";
  constant CMD_PING         : byte_t := x"9F";
  constant REG_ID           : byte_t := x"00";
  constant REG_STATUS       : byte_t := x"01";
  constant REG_FEATURES     : byte_t := x"02";
  constant REG_LED_R        : byte_t := x"10";
  constant REG_LED_G        : byte_t := x"11";
  constant REG_LED_B        : byte_t := x"12";
  constant REG_LED_GLOBAL   : byte_t := x"13";
  constant REG_LED_ENABLE   : byte_t := x"14";
  constant REG_BUTTON       : byte_t := x"20";
  constant REG_BUTTON_COUNT : byte_t := x"21";
  -- REG_TICK_CAPTURE and REG_TICK_0 share an address on purpose, the same way
  -- REG_STATUS means different things read and written: a write latches the
  -- free-running tick counter, a read returns the latched low byte. The capture
  -- name appears only in the write decode and the byte names only in the readback
  -- case, so the duplicate value never collides.
  constant REG_TICK_CAPTURE : byte_t := x"30";
  constant REG_TICK_0       : byte_t := x"30";
  constant REG_TICK_1       : byte_t := x"31";
  constant REG_TICK_2       : byte_t := x"32";
  constant REG_TICK_3       : byte_t := x"33";

end package forgix_pkg;

