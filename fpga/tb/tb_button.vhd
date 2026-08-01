library ieee;
use ieee.std_logic_1164.all;

entity tb_button is
end entity;

architecture sim of tb_button is
  constant PERIOD : time := 10 ns;
  signal clk, rst : std_ulogic := '0';
  signal button_n : std_ulogic := '1';
  signal raw_pressed, pressed, press_strobe : std_ulogic;
begin
  clk <= not clk after PERIOD / 2;

  dut : entity work.forgix_button
    generic map (CLK_HZ => 100_000, DEBOUNCE_MS => 1)
    port map (
      clk => clk, rst => rst, button_n => button_n,
      raw_pressed => raw_pressed, pressed => pressed,
      press_strobe => press_strobe
    );

  process
  begin
    rst <= '1';
    wait for 3 * PERIOD;
    rst <= '0';
    button_n <= '0';
    wait for 50 * PERIOD;
    button_n <= '1';
    wait for 10 * PERIOD;
    button_n <= '0';
    wait for 110 * PERIOD;
    assert pressed = '1' report "button did not debounce pressed" severity failure;
    button_n <= '1';
    wait for 110 * PERIOD;
    assert pressed = '0' report "button did not debounce released" severity failure;
    report "tb_button passed" severity note;
    wait;
  end process;
end architecture;

