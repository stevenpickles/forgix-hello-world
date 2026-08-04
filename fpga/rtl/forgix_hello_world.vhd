-- Top level: the register file, and the wiring between the SPI target, the debouncer
-- and the PWM driver. Everything the MCU can observe or change about this design goes
-- through the register file below, so this is the file that defines what the device
-- actually is; the three sub-entities are mechanism.
--
-- The register file is where the design stops being obvious. Reading a register and
-- writing the same address are not inverses: reads are a pure combinational function
-- of the current state, while a write to REG_STATUS acknowledges sticky bits rather
-- than storing anything. The two directions are built by two separate processes below
-- and share only the address.
--
-- Port names and port order are frozen. fpga/constraints/forgix_hello_world_io.isf
-- binds them to pins by name, and nothing in the VHDL toolchain reads that file, so a
-- rename here fails at place-and-route or, worse, at the bench.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use work.forgix_pkg.all;

entity forgix_hello_world is
  generic (
    CLK_HZ      : positive := 32_000_000;
    DEBOUNCE_MS : positive := 10
  );
  port (
    clk_32m      : in    std_ulogic;
    spi_cs_n     : in    std_ulogic;
    spi_sck      : in    std_ulogic;
    spi_sdio_in  : in    std_ulogic;
    button_n     : in    std_ulogic;
    spi_sdio_out : out   std_ulogic;
    spi_sdio_oe  : out   std_ulogic;
    led_r_n      : out   std_ulogic;
    led_g_n      : out   std_ulogic;
    led_b_n      : out   std_ulogic
  );
end entity forgix_hello_world;

architecture rtl of forgix_hello_world is

  signal por        : unsigned(7 downto 0) := (others => '0');
  signal rst        : std_ulogic;
  signal wr         : std_ulogic;
  signal rd         : std_ulogic;
  signal reset_regs : std_ulogic;
  signal activity   : std_ulogic;
  signal spi_error  : std_ulogic;
  signal addr       : byte_t;
  signal wdata      : byte_t;
  signal rdata      : byte_t;
  -- Power-up state, and the appearance of the board out of configuration: a dim blue
  -- LED at a quarter brightness. These four values are duplicated in the reset arm of
  -- register_file below, because a CMD_RESET has to reproduce the power-up look without
  -- reconfiguring the FPGA. tb_spi_regs checks the pair agree by resetting and reading
  -- back x"20" and x"40", which is the only thing keeping the two copies in step.
  signal red          : byte_t     := x"00";
  signal green        : byte_t     := x"00";
  signal blue         : byte_t     := x"20";
  signal brightness   : byte_t     := x"40";
  signal led_enable   : std_ulogic := '1';
  signal raw_button   : std_ulogic := '0';
  signal button       : std_ulogic := '0';
  signal button_press : std_ulogic := '0';
  signal button_event : std_ulogic := '0';
  signal button_count : byte_t     := x"00";

begin

  -- Power-on reset. This board has no reset pin and no PLL lock to wait on, so the
  -- only reset the design gets is the one it makes for itself: hold rst asserted until
  -- a counter has seen a fixed number of clk_32m edges, which is proof the external
  -- oscillator is actually running rather than still starting.
  --
  -- The counter is a byte because that is the cheapest width that gives a comfortable
  -- margin, not because 256 was measured. 256 cycles is 8 us at 32 MHz. The oscillator
  -- is enabled by the MCU before it starts loading the bitstream and the MCU then tears
  -- down its SPI peripheral and re-initializes GPIO before the first register
  -- transaction, so the real gap is milliseconds. Anything from a few cycles upward
  -- would work; this is sized for headroom and costs one 8-bit counter.
  --
  -- Note the counter saturates rather than wraps, and nothing re-arms it. rst asserts
  -- exactly once per configuration, so this is a startup reset and not a watchdog.
  rst <= '1' when por /= x"FF" else
         '0';

  por_counter : process (clk_32m) is
  begin

    if rising_edge(clk_32m) then
      if por /= x"FF" then
        por <= por + 1;
      end if;
    end if;

  end process por_counter;

  spi : entity work.forgix_spi
    port map (
      clk        => clk_32m,
      rst        => rst,
      cs_n       => spi_cs_n,
      sck        => spi_sck,
      sdio_in    => spi_sdio_in,
      sdio_out   => spi_sdio_out,
      sdio_oe    => spi_sdio_oe,
      reg_write  => wr,
      reg_read   => rd,
      reg_addr   => addr,
      reg_wdata  => wdata,
      reg_rdata  => rdata,
      reset_regs => reset_regs,
      activity   => activity,
      error      => spi_error
    );

  debounce : entity work.forgix_button
    generic map (
      CLK_HZ => CLK_HZ, DEBOUNCE_MS => DEBOUNCE_MS
    )
    port map (
      clk          => clk_32m,
      rst          => rst,
      button_n     => button_n,
      raw_pressed  => raw_button,
      pressed      => button,
      press_strobe => button_press
    );

  pwm : entity work.forgix_rgb_pwm
    port map (
      clk        => clk_32m,
      rst        => rst,
      enable     => led_enable,
      red        => red,
      green      => green,
      blue       => blue,
      brightness => brightness,
      led_r_n    => led_r_n,
      led_g_n    => led_g_n,
      led_b_n    => led_b_n
    );

  -- The writable state. reset_regs is CMD_RESET arriving over SPI and is treated
  -- identically to power-on reset, so a host can restore the device to its
  -- out-of-configuration appearance without reloading the bitstream.
  register_file : process (clk_32m) is
  begin

    if rising_edge(clk_32m) then
      if rst = '1' or reset_regs = '1' then
        red          <= x"00";
        green        <= x"00";
        blue         <= x"20";
        brightness   <= x"40";
        led_enable   <= '1';
        button_event <= '0';
        button_count <= x"00";
      else
        -- button_press is the debouncer's single-cycle strobe, so this counts presses
        -- and not levels. button_event latches until acknowledged, which is what lets
        -- a host poll slowly and still not miss a press.
        if button_press = '1' then
          button_event <= '1';
          -- Saturates at 0xFF instead of wrapping. A wrapped count is
          -- indistinguishable from a small one, so a host that polls late would read
          -- "3 presses" after 259 of them. Sticking at 0xFF at least reads as "at
          -- least 255, you are not keeping up".
          if button_count /= x"FF" then
            button_count <= button_count + 1;
          end if;
        end if;
        if wr = '1' then

          case addr is

            -- REG_STATUS is read/write with different meanings each way. Reading it
            -- returns live status; writing it stores nothing and only acknowledges
            -- sticky bits. Bit 2 is write-one-to-clear on button_event, matching the
            -- bit position the same event is reported in, so a host clears exactly
            -- what it just read. Writing zeros clears nothing, which is what makes a
            -- read-modify-write safe against a press landing in between.
            when REG_STATUS =>

              if wdata(2) = '1' then
                button_event <= '0';
              end if;

            when REG_LED_R =>

              red <= wdata;

            when REG_LED_G =>

              green <= wdata;

            when REG_LED_B =>

              blue <= wdata;

            when REG_LED_GLOBAL =>

              brightness <= wdata;

            when REG_LED_ENABLE =>

              led_enable <= wdata(0);

            -- Only a write of zero does anything, so the register cannot be set to an
            -- arbitrary count and a host cannot fabricate presses. Clearing the count
            -- clears the event with it, because the two describe the same presses and
            -- leaving the event latched after zeroing the count would report a press
            -- the count no longer admits to.
            when REG_BUTTON_COUNT =>

              if wdata = x"00" then
                button_count <= x"00";
                button_event <= '0';
              end if;

            when others =>

              null;

          end case;

        end if;
      end if;
    end if;

  end process register_file;

  -- Read side of the register file: combinational, process (all), and holding no state
  -- of its own. Reads therefore have no side effects and cost the SPI engine a single
  -- cycle in read_wait_s.
  readback_mux : process (all) is
  begin

    -- Default first, which is what stops this inferring a latch: every path through
    -- the case leaves rdata assigned, including the arms that fall through to null.
    --
    -- x"EE" rather than zero so an unmapped address is distinguishable from a register
    -- that genuinely holds zero. A host reading a register this design does not
    -- implement gets an answer that looks wrong on sight instead of a plausible one.
    rdata <= x"EE";

    case addr is

      when REG_ID =>

        rdata <= DESIGN_ID;

      -- Bit 0 is a constant '1'. It is not a flag about anything: it is the evidence
      -- that this multiplexer answered at all, which distinguishes a live design from
      -- a floating bus reading back as all zeros.
      when REG_STATUS =>

        rdata    <= (others => '0');
        rdata(0) <= '1';
        rdata(1) <= button;
        rdata(2) <= button_event;
        rdata(3) <= activity;
        rdata(4) <= raw_button;
        rdata(7) <= spi_error;

      when REG_FEATURES =>

        rdata <= x"03";

      when REG_LED_R =>

        rdata <= red;

      when REG_LED_G =>

        rdata <= green;

      when REG_LED_B =>

        rdata <= blue;

      when REG_LED_GLOBAL =>

        rdata <= brightness;

      when REG_LED_ENABLE =>

        rdata <= (0 => led_enable, others => '0');

      when REG_BUTTON =>

        rdata <= (0 => button, 1 => raw_button, others => '0');

      when REG_BUTTON_COUNT =>

        rdata <= button_count;

      when others =>

        null;

    end case;

  end process readback_mux;

end architecture rtl;

