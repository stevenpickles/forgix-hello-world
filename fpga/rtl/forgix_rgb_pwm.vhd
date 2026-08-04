-- Drives the RGB LED by pulse width modulation, with a per-channel intensity and one
-- global brightness that scales all three together. The channel and the brightness are
-- multiplied rather than applied in sequence, so brightness is a true master fader:
-- halving it halves every channel's on-time and leaves the colour unchanged.
--
-- Two things here surprise people. The outputs are active low -- '0' is lit -- so the
-- comparisons below read backwards from what the signal names suggest. And a channel
-- can never be lit for all 256 phases: the duty comes from the top byte of an 8x8
-- product, which reaches 255 at most, so full white is 255/256 rather than continuous.
-- That ceiling is invisible to the eye and is the reason the PWM bench expects 127 and
-- not 128 at half brightness.
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

  -- 8x8 unsigned, so these are 16 bits wide and only the top byte is ever used. Kept
  -- as concurrent assignments rather than folded into the comparisons below so the
  -- multiply is shared by all three comparisons instead of being inferred repeatedly.
  r_scaled <= red * brightness;
  g_scaled <= green * brightness;
  b_scaled <= blue * brightness;

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

  -- Duty is the high byte of the product, which is the same as (channel * brightness)
  -- / 256 -- the truncation is the divide, not a rounding error. Taking the low byte
  -- instead would still light the LED and still respond to both inputs, which is
  -- exactly why the PWM bench counts on-phases rather than eyeballing the result.
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

