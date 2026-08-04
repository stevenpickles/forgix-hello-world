-- Guards the debouncer against the two failures that a filter which "works" on the
-- bench still has: believing a bounce, and never believing anything.
--
-- The stimulus presses, releases after 10 cycles, and presses again -- a release
-- shorter than the stability window, which is exactly what contact bounce looks like.
-- A filter that restarts its count on any disagreement rides through it; one that
-- instead counts elapsed time since the first edge latches the spurious release and
-- fails the first assert. The second assert catches the opposite defect, a filter so
-- eager to reset its counter that a genuine release never completes.
--
-- Both asserts are on `pressed` rather than `press_strobe` because the strobe is a
-- single cycle and would need sampling at the right moment; the level is the thing a
-- consumer of this module actually sees.

library ieee;
  use ieee.std_logic_1164.all;
  use std.env.all;

entity tb_button is
end entity tb_button;

architecture sim of tb_button is

  constant PERIOD       : time       := 10 ns;
  signal   clk          : std_ulogic := '0';
  signal   rst          : std_ulogic := '0';
  signal   button_n     : std_ulogic := '1';
  signal   raw_pressed  : std_ulogic;
  signal   pressed      : std_ulogic;
  signal   press_strobe : std_ulogic;

begin

  clk <= not clk after PERIOD / 2;

  -- CLK_HZ is a lie told to shrink the filter. The real design debounces 320_000
  -- cycles; at 100_000 with DEBOUNCE_MS = 1 the window is 100 cycles, which is short
  -- enough to simulate and still large enough that the 10-cycle glitch below sits well
  -- inside it. DEBOUNCE_MS is left alone deliberately -- overriding it would exercise
  -- an arithmetic path the shipped configuration never takes.
  --
  -- The waits downstream are written in multiples of PERIOD so they follow this
  -- choice. 110 * PERIOD clears the 100-cycle window with margin for the synchronizer.
  dut : entity work.forgix_button
    generic map (
      CLK_HZ => 100_000, DEBOUNCE_MS => 1
    )
    port map (
      clk          => clk,
      rst          => rst,
      button_n     => button_n,
      raw_pressed  => raw_pressed,
      pressed      => pressed,
      press_strobe => press_strobe
    );

  stimulus : process is
  begin

    rst      <= '1';
    wait for 3 * PERIOD;
    rst      <= '0';
    button_n <= '0';
    wait for 50 * PERIOD;
    -- A 10-cycle release inside a 100-cycle window: bounce, not a real release. The
    -- filter must not see it, and the assert below is what says so.
    button_n <= '1';
    wait for 10 * PERIOD;
    button_n <= '0';
    wait for 110 * PERIOD;
    assert pressed = '1'
      report "button did not debounce pressed"
      severity failure;
    button_n <= '1';
    wait for 110 * PERIOD;
    assert pressed = '0'
      report "button did not debounce released"
      severity failure;
    report "tb_button passed"
      severity note;
    stop;
    wait;

  end process stimulus;

end architecture sim;
