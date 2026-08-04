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

  constant STABLE_CYCLES : positive                             := (CLK_HZ / 1000) * DEBOUNCE_MS;
  signal   sync          : std_ulogic_vector(1 downto 0)        := (others => '1');
  signal   stable        : std_ulogic                           := '1';
  signal   counter       : natural range 0 to STABLE_CYCLES - 1 := 0;

begin

  raw_pressed <= not sync(1);
  pressed     <= not stable;

  debounce : process (clk) is
  begin

    if rising_edge(clk) then
      sync         <= sync(0) & button_n;
      press_strobe <= '0';
      if rst = '1' then
        stable  <= '1';
        counter <= 0;
      elsif sync(1) = stable then
        counter <= 0;
      elsif counter = STABLE_CYCLES - 1 then
        stable  <= sync(1);
        counter <= 0;
        if sync(1) = '0' then
          press_strobe <= '1';
        end if;
      else
        counter <= counter + 1;
      end if;
    end if;

  end process debounce;

end architecture rtl;

