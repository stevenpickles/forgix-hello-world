-- Guards the PWM duty cycle against the scaling arithmetic being truncated at the
-- wrong end. Red at 0xFF with brightness 0x80 must be lit for exactly 128 of 256
-- phases: the duty is the high byte of channel * (brightness + 1), 255 * 129 / 256,
-- and the comparison against phase is strict. An off-by-one in the
-- r_scaled(15 downto 8) slice, or taking the low byte instead, still produces a lit
-- LED that responds to both inputs -- it fails here rather than as a dim LED on the
-- bench, which is the entire reason this counts phases instead of watching a pin.
--
-- The green channel at half the intensity pins the ratio as well as the midpoint, so a
-- scaling bug that happens to land on 128 for red still has to explain 64 for green.
-- Blue at zero guards one boundary: strict comparison means zero is off for all 256
-- phases, and a <= would light it for one. The second round pins the other boundary,
-- 0xFF at 0xFF: brightness full is unity gain, so a full channel must be lit 255 of
-- 256 phases -- the plain channel * brightness arithmetic lands on 254 and fails it.
--
-- The last check is about the enable path only, and exists because an active-low
-- output makes "off" the value a broken design is most likely to produce by accident.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use std.env.all;

entity tb_pwm is
end entity tb_pwm;

architecture sim of tb_pwm is

  signal clk        : std_ulogic           := '0';
  signal rst        : std_ulogic           := '0';
  signal enable     : std_ulogic           := '0';
  signal red        : unsigned(7 downto 0) := (others => '0');
  signal green      : unsigned(7 downto 0) := (others => '0');
  signal blue       : unsigned(7 downto 0) := (others => '0');
  signal brightness : unsigned(7 downto 0) := (others => '0');
  signal led_r_n    : std_ulogic;
  signal led_g_n    : std_ulogic;
  signal led_b_n    : std_ulogic;

begin

  clk <= not clk after 5 ns;

  dut : entity work.forgix_rgb_pwm
    port map (
      clk        => clk,
      rst        => rst,
      enable     => enable,
      red        => red,
      green      => green,
      blue       => blue,
      brightness => brightness,
      led_r_n    => led_r_n,
      led_g_n    => led_g_n,
      led_b_n    => led_b_n
    );

  stimulus : process is

    variable red_on   : natural := 0;
    variable green_on : natural := 0;
    variable blue_on  : natural := 0;

  begin

    rst        <= '1';
    wait until rising_edge(clk);
    wait until rising_edge(clk);
    rst        <= '0';
    enable     <= '1';
    red        <= x"FF";
    green      <= x"80";
    blue       <= x"00";
    brightness <= x"80";

    -- Exactly one full PWM period: phase is 8 bits and free-running, so 256 clocks
    -- visits every phase value once. Sampling more or fewer would make the counts
    -- below depend on where the loop started relative to phase.
    for cycle in 0 to 255 loop

      wait until rising_edge(clk);
      -- Settle past the delta cycles before sampling. The outputs are combinational
      -- functions of phase, which has only just been clocked, so reading them in the
      -- same delta would see the previous phase's decision.
      wait for 1 ns;

      if led_r_n = '0' then
        red_on := red_on + 1;
      end if;

      if led_g_n = '0' then
        green_on := green_on + 1;
      end if;

      if led_b_n = '0' then
        blue_on := blue_on + 1;
      end if;

    end loop;

    assert red_on = 128
      report "red PWM duty cycle mismatch"
      severity failure;
    assert green_on = 64
      report "green PWM duty cycle mismatch"
      severity failure;
    assert blue_on = 0
      report "zero blue intensity should remain off"
      severity failure;

    -- Full brightness is unity gain, so a full channel is lit for every phase but
    -- the strict comparison's last, and half intensity passes through unscaled.
    red_on     := 0;
    green_on   := 0;
    blue_on    := 0;
    brightness <= x"FF";

    for cycle in 0 to 255 loop

      wait until rising_edge(clk);
      wait for 1 ns;

      if led_r_n = '0' then
        red_on := red_on + 1;
      end if;

      if led_g_n = '0' then
        green_on := green_on + 1;
      end if;

      if led_b_n = '0' then
        blue_on := blue_on + 1;
      end if;

    end loop;

    assert red_on = 255
      report "full channel at full brightness must be lit 255 of 256 phases"
      severity failure;
    assert green_on = 128
      report "full brightness must pass half intensity through unscaled"
      severity failure;
    assert blue_on = 0
      report "zero blue intensity should remain off at full brightness"
      severity failure;

    enable <= '0';
    wait for 1 ns;
    assert led_r_n = '1' and led_g_n = '1' and led_b_n = '1'
      report "disabled active-low LEDs must all be off"
      severity failure;

    report "PWM checks passed"
      severity note;
    stop;
    wait;

  end process stimulus;

end architecture sim;
