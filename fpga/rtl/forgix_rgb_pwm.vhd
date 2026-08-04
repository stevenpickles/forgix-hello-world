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

  signal phase    : unsigned(7 downto 0)  := (others => '0');
  signal r_scaled : unsigned(15 downto 0) := (others => '0');
  signal g_scaled : unsigned(15 downto 0) := (others => '0');
  signal b_scaled : unsigned(15 downto 0) := (others => '0');

begin

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

  led_r_n <= '0' when enable = '1' and phase < r_scaled(15 downto 8) else
             '1';
  led_g_n <= '0' when enable = '1' and phase < g_scaled(15 downto 8) else
             '1';
  led_b_n <= '0' when enable = '1' and phase < b_scaled(15 downto 8) else
             '1';

end architecture rtl;

