library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.forgix_pkg.all;

entity forgix_hello_world is
  port (
    clk_32m, spi_cs_n, spi_sck, spi_sdio_in, button_n : in std_ulogic;
    spi_sdio_out, spi_sdio_oe : out std_ulogic;
    led_r_n, led_g_n, led_b_n : out std_ulogic
  );
end entity;

architecture rtl of forgix_hello_world is
  signal por : unsigned(7 downto 0) := (others => '0');
  signal rst, wr, rd, reset_regs, activity, spi_error : std_ulogic;
  signal addr, wdata, rdata : byte_t;
  signal red : byte_t := x"00"; signal green : byte_t := x"00";
  signal blue : byte_t := x"20"; signal brightness : byte_t := x"40";
  signal led_enable : std_ulogic := '1';
  signal raw_button, button, button_press, button_event : std_ulogic := '0';
  signal button_count : byte_t := x"00";
begin
  rst <= '1' when por /= x"FF" else '0';
  process(clk_32m)
  begin
    if rising_edge(clk_32m) then
      if por /= x"FF" then
        por <= por + 1;
      end if;
    end if;
  end process;

  spi : entity work.forgix_spi port map (
    clk_32m, rst, spi_cs_n, spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe,
    wr, rd, addr, wdata, rdata, reset_regs, activity, spi_error);
  debounce : entity work.forgix_button port map (
    clk_32m, rst, button_n, raw_button, button, button_press);
  pwm : entity work.forgix_rgb_pwm port map (
    clk_32m, rst, led_enable, red, green, blue, brightness, led_r_n, led_g_n, led_b_n);

  process(clk_32m)
  begin
    if rising_edge(clk_32m) then
      if rst = '1' or reset_regs = '1' then
        red <= x"00"; green <= x"00"; blue <= x"20"; brightness <= x"40";
        led_enable <= '1'; button_event <= '0'; button_count <= x"00";
      else
        if button_press = '1' then
          button_event <= '1';
          if button_count /= x"FF" then button_count <= button_count + 1; end if;
        end if;
        if wr = '1' then
          case addr is
            when REG_STATUS => if wdata(2) = '1' then button_event <= '0'; end if;
            when REG_LED_R => red <= wdata;
            when REG_LED_G => green <= wdata;
            when REG_LED_B => blue <= wdata;
            when REG_LED_GLOBAL => brightness <= wdata;
            when REG_LED_ENABLE => led_enable <= wdata(0);
            when REG_BUTTON_COUNT =>
              if wdata = x"00" then button_count <= x"00"; button_event <= '0'; end if;
            when others => null;
          end case;
        end if;
      end if;
    end if;
  end process;

  process(all)
  begin
    rdata <= x"EE";
    case addr is
      when REG_ID => rdata <= DESIGN_ID;
      when REG_STATUS =>
        rdata <= (others => '0'); rdata(0) <= '1'; rdata(1) <= button;
        rdata(2) <= button_event; rdata(3) <= activity; rdata(4) <= raw_button;
        rdata(7) <= spi_error;
      when REG_FEATURES => rdata <= x"03";
      when REG_LED_R => rdata <= red;
      when REG_LED_G => rdata <= green;
      when REG_LED_B => rdata <= blue;
      when REG_LED_GLOBAL => rdata <= brightness;
      when REG_LED_ENABLE => rdata <= (0 => led_enable, others => '0');
      when REG_BUTTON => rdata <= (0 => button, 1 => raw_button, others => '0');
      when REG_BUTTON_COUNT => rdata <= button_count;
      when others => null;
    end case;
  end process;
end architecture;

