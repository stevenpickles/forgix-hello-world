library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;
use work.forgix_pkg.all;

entity tb_spi_regs is
end entity;

architecture sim of tb_spi_regs is
  signal clk : std_ulogic := '0';
  signal spi_cs_n, spi_sck, spi_sdio_in : std_ulogic := '1';
  signal spi_sdio_out, spi_sdio_oe : std_ulogic;
  signal button_n : std_ulogic := '1';
  signal led_r_n, led_g_n, led_b_n : std_ulogic;

  procedure send_byte(
    signal sck : out std_ulogic;
    signal sdio : out std_ulogic;
    constant value : in byte_t
  ) is
  begin
    for bit_index in 7 downto 0 loop
      sdio <= value(bit_index);
      wait for 40 ns;
      sck <= '1';
      wait for 40 ns;
      sck <= '0';
    end loop;
  end procedure;

  procedure send_read_request_byte(
    signal sck : out std_ulogic;
    signal sdio : out std_ulogic;
    signal dut_oe : in std_ulogic;
    constant value : in byte_t
  ) is
  begin
    for bit_index in 7 downto 1 loop
      sdio <= value(bit_index);
      wait for 40 ns;
      sck <= '1';
      wait for 40 ns;
      sck <= '0';
    end loop;
    sdio <= value(0);
    wait for 40 ns;
    sck <= '1';
    wait for 20 ns;
    assert dut_oe = '0' report "FPGA drove SDIO before MCU turnaround" severity failure;
    wait for 20 ns;
    sck <= '0';
    wait for 40 ns;
    assert dut_oe = '1' report "FPGA did not drive SDIO after turnaround" severity failure;
  end procedure;

  procedure receive_byte(
    signal sck : out std_ulogic;
    signal sdio : out std_ulogic;
    signal dut_sdio : in std_ulogic;
    signal dut_oe : in std_ulogic;
    variable value : out byte_t
  ) is
  begin
    sdio <= '0';
    value := (others => '0');
    wait for 40 ns;
    assert dut_oe = '1' report "FPGA did not take control of SDIO" severity failure;
    for bit_index in 7 downto 0 loop
      sck <= '1';
      wait for 20 ns;
      value(bit_index) := dut_sdio;
      wait for 20 ns;
      sck <= '0';
      wait for 40 ns;
    end loop;
  end procedure;

  procedure begin_transaction(signal cs_n : out std_ulogic) is
  begin
    cs_n <= '0';
    wait for 80 ns;
  end procedure;

  procedure end_transaction(signal cs_n : out std_ulogic) is
  begin
    cs_n <= '1';
    wait for 100 ns;
  end procedure;
begin
  clk <= not clk after 5 ns;

  dut : entity work.forgix_hello_world
    generic map (CLK_HZ => 1_000, DEBOUNCE_MS => 1)
    port map (clk, spi_cs_n, spi_sck, spi_sdio_in, button_n,
              spi_sdio_out, spi_sdio_oe, led_r_n, led_g_n, led_b_n);

  stimulus : process
    variable result : byte_t;

    procedure ping(variable value : out byte_t) is
    begin
      begin_transaction(spi_cs_n);
      send_read_request_byte(spi_sck, spi_sdio_in, spi_sdio_oe, CMD_PING);
      receive_byte(spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe, value);
      end_transaction(spi_cs_n);
    end procedure;

    procedure write_register(constant address, value : in byte_t) is
    begin
      begin_transaction(spi_cs_n);
      send_byte(spi_sck, spi_sdio_in, CMD_WRITE);
      send_byte(spi_sck, spi_sdio_in, address);
      send_byte(spi_sck, spi_sdio_in, value);
      end_transaction(spi_cs_n);
    end procedure;

    procedure read_register(constant address : in byte_t; variable value : out byte_t) is
    begin
      begin_transaction(spi_cs_n);
      send_byte(spi_sck, spi_sdio_in, CMD_READ);
      send_read_request_byte(spi_sck, spi_sdio_in, spi_sdio_oe, address);
      receive_byte(spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe, value);
      end_transaction(spi_cs_n);
    end procedure;
  begin
    spi_sck <= '0';
    wait for 3 us;

    ping(result);
    assert result = DESIGN_ID report "ping did not return the design ID" severity failure;

    write_register(REG_LED_R, x"12");
    write_register(REG_LED_G, x"34");
    write_register(REG_LED_B, x"56");
    write_register(REG_LED_GLOBAL, x"78");
    write_register(REG_LED_ENABLE, x"00");
    read_register(REG_LED_R, result);
    assert result = x"12" report "red register readback mismatch" severity failure;
    read_register(REG_LED_G, result);
    assert result = x"34" report "green register readback mismatch" severity failure;
    read_register(REG_LED_B, result);
    assert result = x"56" report "blue register readback mismatch" severity failure;
    read_register(REG_LED_GLOBAL, result);
    assert result = x"78" report "global brightness readback mismatch" severity failure;
    read_register(REG_LED_ENABLE, result);
    assert result = x"00" report "LED enable readback mismatch" severity failure;

    button_n <= '0';
    wait for 100 ns;
    button_n <= '1';
    wait for 100 ns;
    read_register(REG_BUTTON_COUNT, result);
    assert result = x"01" report "button press was not counted" severity failure;
    read_register(REG_STATUS, result);
    assert result(2) = '1' report "button event was not latched" severity failure;
    write_register(REG_BUTTON_COUNT, x"00");
    read_register(REG_BUTTON_COUNT, result);
    assert result = x"00" report "button count clear failed" severity failure;

    begin_transaction(spi_cs_n);
    send_byte(spi_sck, spi_sdio_in, CMD_RESET);
    end_transaction(spi_cs_n);
    read_register(REG_LED_B, result);
    assert result = x"20" report "reset did not restore blue default" severity failure;
    read_register(REG_LED_GLOBAL, result);
    assert result = x"40" report "reset did not restore brightness default" severity failure;
    read_register(REG_LED_ENABLE, result);
    assert result = x"01" report "reset did not enable LEDs" severity failure;

    begin_transaction(spi_cs_n);
    send_byte(spi_sck, spi_sdio_in, x"55");
    end_transaction(spi_cs_n);
    read_register(REG_STATUS, result);
    assert result(7) = '1' report "invalid command did not set SPI error" severity failure;

    report "SPI register checks passed" severity note;
    stop;
    wait;
  end process;
end architecture;
