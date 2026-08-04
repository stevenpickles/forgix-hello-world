-- Drives the RGB LED by pulse width modulation, with a per-channel intensity and one
-- global brightness that scales all three together. The channel and the brightness are
-- multiplied rather than applied in sequence, so brightness is a true master fader:
-- halving it halves every channel's on-time and leaves the colour unchanged.
--
-- Two things here surprise people. The outputs are active low -- '0' is lit -- so the
-- comparisons below read backwards from what the signal names suggest. And a channel
-- can never be lit for all 256 phases: the duty is the top byte of channel times
-- (brightness + 1), so brightness 0xFF is exact unity gain -- the duty equals the
-- channel -- and full white is 255/256 by the strict comparison rather than
-- continuous. Half brightness (0x80) is a 129/256 scale, which is the reason the PWM
-- bench expects 128 and not 127 for a full channel.
--
-- The phase counter free-runs off clk with no prescaler, so the refresh rate is the
-- clock divided by 256 -- 125 kHz at the real 32 MHz, far above anything visible.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;

entity forgix_rgb_pwm is
  port (
    clk        : in    std_ulogic;
    rst        : in    std_ulogic;
    enable     : in    std_ulogic;
    red        : in    unsigned(7 downto 0);
    green      : in    unsigned(7 downto 0);
    blue       : in    unsigned(7 downto 0);
    brightness : in    unsigned(7 downto 0);
    led_r_n    : out   std_ulogic;
    led_g_n    : out   std_ulogic;
    led_b_n    : out   std_ulogic
  );
end entity forgix_rgb_pwm;

architecture rtl of forgix_rgb_pwm is

  -- Power-up state. Starting phase at zero is what makes the very first PWM period a
  -- full one; without it the LED's first cycle would be an arbitrary fraction.
  signal phase    : unsigned(7 downto 0)  := (others => '0');
  signal r_scaled : unsigned(15 downto 0) := (others => '0');
  signal g_scaled : unsigned(15 downto 0) := (others => '0');
  signal b_scaled : unsigned(15 downto 0) := (others => '0');

begin

  -- channel * (brightness + 1), written as the product plus the channel so the
  -- operands stay 8x8 and the sum peaks at 0xFF00 -- still 16 bits, no overflow. A
  -- plain channel * brightness tops out at 0xFE01, whose high byte is 254: full
  -- white would quietly become 254/256 and 0xFF brightness would not be unity gain.
  -- Kept as concurrent assignments rather than folded into the comparisons below so
  -- the arithmetic is shared by all three comparisons instead of being inferred
  -- repeatedly.
  r_scaled <= red * brightness + red;
  g_scaled <= green * brightness + green;
  b_scaled <= blue * brightness + blue;

  phase_counter : process (clk) is
  begin

    if rising_edge(clk) then
      if rst = '1' then
        phase <= (others => '0');
      else
        phase <= phase + 1;
      end if;
    end if;

  end process phase_counter;

  -- Duty is the high byte of channel * (brightness + 1), which is the same as
  -- dividing by 256 -- the truncation is the divide, not a rounding error. Taking
  -- the low byte instead would still light the LED and still respond to both
  -- inputs, which is exactly why the PWM bench counts on-phases rather than
  -- eyeballing the result.
  --
  -- The comparison is strict, so a scaled value of zero is off for all 256 phases and
  -- a channel written to zero goes truly dark rather than flickering once per period.
  led_r_n <= '0' when enable = '1' and phase < r_scaled(15 downto 8) else
             '1';
  led_g_n <= '0' when enable = '1' and phase < g_scaled(15 downto 8) else
             '1';
  led_b_n <= '0' when enable = '1' and phase < b_scaled(15 downto 8) else
             '1';

end architecture rtl;

