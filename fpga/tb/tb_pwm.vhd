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

    assert red_on = 127
      report "red PWM duty cycle mismatch"
      severity failure;
    assert green_on = 64
      report "green PWM duty cycle mismatch"
      severity failure;
    assert blue_on = 0
      report "zero blue intensity should remain off"
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
