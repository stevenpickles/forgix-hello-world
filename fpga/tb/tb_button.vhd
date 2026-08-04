-- Guards the debouncer against the failures that a filter which "works" on the bench
-- still has: believing a bounce, accepting before the window has re-run clean, and
-- strobing wrongly.
--
-- The stimulus presses, then bounces -- a 10-cycle release -- straddling the moment a
-- naive filter's window first expires, roughly 100 cycles after the press. That
-- placement is the test: a correct filter restarts its count at the bounce and does
-- not accept until 100 uninterrupted cycles after it, so the mid-bounce checkpoint at
-- 203 cycles catches both a free-running sampler (which accepted at its wrap, inside
-- or just after the glitch) and a counter that pauses without resetting (which
-- accepted a few cycles after the glitch ended). The release asserts catch the
-- opposite defect, a filter so eager to reset that a genuine release never completes.
--
-- Levels are checked on `pressed`; counting is checked on `press_strobe` through a
-- clocked counter, because a single-cycle pulse cannot be caught by level asserts at
-- chosen instants -- and the strobe is the one output the header of the RTL says a
-- press counter must use. raw_pressed is checked against the synchronized input on
-- both sides.

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
  signal   strobe_count : natural    := 0;

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

  -- press_strobe is one clk wide, so a counter clocked beside the DUT is the only
  -- observer that can prove "exactly one strobe per accepted press": level asserts at
  -- chosen instants would miss a double strobe or a strobe on release entirely.
  count_strobes : process (clk) is
  begin

    if rising_edge(clk) then
      if press_strobe = '1' then
        strobe_count <= strobe_count + 1;
      end if;
    end if;

  end process count_strobes;

  stimulus : process is
  begin

    rst      <= '1';
    wait for 3 * PERIOD;
    rst      <= '0';
    button_n <= '0';
    -- 95 press cycles, then a 10-cycle release: bounce, not a real release, placed to
    -- straddle the ~100-cycle mark where a filter that never restarted its count
    -- first expires. A correct filter accepts 100 clean cycles after the bounce ends;
    -- a free-running or pause-without-reset one has already accepted by the
    -- checkpoint below.
    wait for 95 * PERIOD;
    button_n <= '1';
    wait for 10 * PERIOD;
    button_n <= '0';
    -- 98 cycles in: past every wrong filter's acceptance, still 2 cycles and a
    -- synchronizer short of the correct one's.
    wait for 98 * PERIOD;
    assert pressed = '0'
      report "button accepted the press before the bounce-restarted window elapsed"
      severity failure;
    assert raw_pressed = '1'
      report "raw_pressed does not follow the synchronized input while pressed"
      severity failure;
    assert strobe_count = 0
      report "press_strobe fired before the press was accepted"
      severity failure;
    wait for 10 * PERIOD;
    assert pressed = '1'
      report "button did not debounce pressed"
      severity failure;
    assert strobe_count = 1
      report "an accepted press must strobe exactly once"
      severity failure;
    button_n <= '1';
    wait for 110 * PERIOD;
    assert pressed = '0'
      report "button did not debounce released"
      severity failure;
    assert raw_pressed = '0'
      report "raw_pressed does not follow the synchronized input when released"
      severity failure;
    assert strobe_count = 1
      report "release must not strobe"
      severity failure;
    report "tb_button passed"
      severity note;
    stop;
    wait;

  end process stimulus;

end architecture sim;
