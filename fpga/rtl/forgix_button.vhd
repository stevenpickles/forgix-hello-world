-- Debounces the front-panel button by refusing to believe a level change until the
-- synchronized input has held it for DEBOUNCE_MS. Any bounce back inside that window
-- restarts the count, so a noisy contact costs latency and never a false edge.
--
-- The three outputs are deliberately different things and a consumer that picks the
-- wrong one is the mistake this header exists to prevent. `raw_pressed` is the
-- synchronized input with no filtering at all and is only useful for diagnostics.
-- `pressed` is the debounced level. `press_strobe` is a single clk-wide pulse on the
-- press edge only -- there is no release strobe -- so anything counting presses must
-- use the strobe. Counting edges on `pressed` instead would double-count, because
-- `pressed` also changes on release.
--
-- Everything is synchronous to clk. rst is synchronous too, and clears the filter to
-- the released state rather than to whatever the button currently reads.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;

entity forgix_button is
  generic (
    CLK_HZ      : positive := 32_000_000;
    DEBOUNCE_MS : positive := 10
  );
  port (
    clk          : in    std_ulogic;
    rst          : in    std_ulogic;
    button_n     : in    std_ulogic;
    raw_pressed  : out   std_ulogic;
    pressed      : out   std_ulogic;
    press_strobe : out   std_ulogic
  );
end entity forgix_button;

architecture rtl of forgix_button is

  -- STABLE_CYCLES is why the generics exist. At the real 32 MHz it is 320_000 cycles,
  -- which no testbench can afford to wait through, so the benches override CLK_HZ with
  -- a fictitious small value to shrink the window rather than changing DEBOUNCE_MS and
  -- testing a different filter from the one that ships.
  constant STABLE_CYCLES : positive := (CLK_HZ / 1000) * DEBOUNCE_MS;

  -- Power-up state. These initializers are the FPGA's configuration-time values, not
  -- tidiness: the idle levels are '1' because the button is active low, so the filter
  -- starts up believing the button is released instead of resolving from 'U'.
  signal sync    : std_ulogic_vector(1 downto 0)        := (others => '1');
  signal stable  : std_ulogic                           := '1';
  signal counter : natural range 0 to STABLE_CYCLES - 1 := 0;

begin

  -- Inverted because button_n is active low: a '0' on the pin is a press.
  raw_pressed <= not sync(1);
  pressed     <= not stable;

  debounce : process (clk) is
  begin

    if rising_edge(clk) then
      -- sync(1) is the twice-registered sample and the only one the filter reads.
      -- sync(0) has been through one flop and may still be resolving, since button_n
      -- arrives from a pin with no relationship to clk.
      sync <= sync(0) & button_n;
      -- Default low every cycle so the assignments below produce a pulse exactly one
      -- clk wide. An arm further down overrides this for the single cycle it fires.
      press_strobe <= '0';
      if rst = '1' then
        stable  <= '1';
        counter <= 0;
      elsif sync(1) = stable then
        -- Input agrees with what we already believe, so any bounce in progress is
        -- abandoned. This is what makes the window "stable for DEBOUNCE_MS" rather
        -- than "DEBOUNCE_MS since the first edge".
        counter <= 0;
      elsif counter = STABLE_CYCLES - 1 then
        stable  <= sync(1);
        counter <= 0;
        -- Strobe on the press edge only. sync(1) = '0' is a press, and it can only
        -- differ from stable here, so this arm cannot fire twice for one press.
        if sync(1) = '0' then
          press_strobe <= '1';
        end if;
      else
        counter <= counter + 1;
      end if;
    end if;

  end process debounce;

end architecture rtl;

