library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;

package forgix_pkg is

  subtype byte_t is unsigned(7 downto 0);

  constant DESIGN_ID        : byte_t := x"B5";
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

end package forgix_pkg;

