-- Guards the MCU-facing contract of the whole design, driving the top-level entity
-- through the same transaction shapes bsp_fpga.c bit-bangs. Its real subject is the
-- half-duplex turnaround on SDIO, which is the one place where both ends can drive the
-- same wire and where a mistake shows up as contention on hardware rather than as a
-- wrong value anywhere.
--
-- send_read_request_byte is the bench's whole point. It clocks the final bit of a read
-- request and pins the turnaround with three asserts: mid-high-phase before the RTL
-- can even have seen the edge (the master owns the line, unconditionally), late in the
-- same high phase after the RTL has had time to react wrongly (this is the one that
-- catches a design driving oe straight out of read_wait_s -- the synchronizer puts the
-- earliest wrong drive three clk cycles after the edge, later than the first assert
-- samples), and after the falling edge, by which time the handover must have happened.
-- Deleting the RTL's tx_arm_s state breaks neither the readback values nor the
-- protocol -- every register still returns the right byte -- so without the middle
-- assert the regression would be invisible here and would appear as a contended bus on
-- the board.
--
-- Everything after that is register semantics that cannot be checked from the RTL in
-- isolation: that reset restores the power-up LED values, that the button count
-- survives a round trip and clears on a zero write, and that an unrecognized command
-- sets a status bit that is still set by the time a separate transaction reads it.
--
-- This bench binds the top-level entity, whose port order is frozen by the pin
-- constraints. That is why the instantiation below is named rather than positional.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use std.env.all;
  use work.forgix_pkg.all;

entity tb_spi_regs is
end entity tb_spi_regs;

architecture sim of tb_spi_regs is

  signal clk          : std_ulogic := '0';
  signal spi_cs_n     : std_ulogic := '1';
  signal spi_sck      : std_ulogic := '1';
  signal spi_sdio_in  : std_ulogic := '1';
  signal spi_sdio_out : std_ulogic;
  signal spi_sdio_oe  : std_ulogic;
  signal button_n     : std_ulogic := '1';
  signal led_r_n      : std_ulogic;
  signal led_g_n      : std_ulogic;
  signal led_b_n      : std_ulogic;

  -- 40 ns per half period, against a 10 ns clk: four clk cycles per sck level. The RTL
  -- oversamples sck through a three-stage synchronizer and detects edges two stages
  -- deep, so an sck level narrower than about three clk cycles would not be seen at
  -- all. Four is deliberately close to that floor -- the real bus runs ~64 cycles per
  -- half period, and a bench at the same ratio would prove nothing about the sampling.

  procedure send_byte (
    signal sck     : out std_ulogic;
    signal sdio    : out std_ulogic;
    constant value : in byte_t
  ) is
  begin

    for bit_index in 7 downto 0 loop

      sdio <= value(bit_index);
      wait for 40 ns;
      sck  <= '1';
      wait for 40 ns;
      sck  <= '0';

    end loop;

  end procedure send_byte;

  -- Sends the last byte of a read request, which is the byte the bus turns around on.
  -- Identical to send_byte for the first seven bits; the eighth is written out longhand
  -- because the two asserts have to land inside it. Modelled on the MCU releasing SDIO
  -- partway through the final high phase, which is what _SendByte does in bsp_fpga.c.

  procedure send_read_request_byte (
    signal sck     : out std_ulogic;
    signal sdio    : out std_ulogic;
    signal dut_oe  : in std_ulogic;
    constant value : in byte_t
  ) is
  begin

    for bit_index in 7 downto 1 loop

      sdio <= value(bit_index);
      wait for 40 ns;
      sck  <= '1';
      wait for 40 ns;
      sck  <= '0';

    end loop;

    sdio <= value(0);
    wait for 40 ns;
    sck  <= '1';
    -- Mid-high-phase of the last bit. The master still owns the line here, so the FPGA
    -- must not be driving; if it is, both ends are.
    wait for 20 ns;
    assert dut_oe = '0'
      report "FPGA drove SDIO before MCU turnaround"
      severity failure;
    -- The bench clock puts edges at 5 mod 10 ns, so the RTL detects the rise above at
    -- T+25 and a design that armed oe straight out of read_wait_s drives from T+35.
    -- Sampling at T+39 -- after that earliest wrong reaction, before the falling edge
    -- -- is what makes premature drive fail instead of slipping between the assert
    -- above (T+20, before the RTL can have reacted at all) and the post-fall one
    -- below.
    wait for 19 ns;
    assert dut_oe = '0'
      report "FPGA drove SDIO before the turnaround falling edge"
      severity failure;
    wait for 1 ns;
    sck <= '0';
    -- One falling edge later the handover must have happened, or the reply's first bit
    -- is clocked out of an undriven line. The RTL's tx_arm_s state is what puts the
    -- transition here and nowhere else.
    wait for 40 ns;
    assert dut_oe = '1'
      report "FPGA did not drive SDIO after turnaround"
      severity failure;

  end procedure send_read_request_byte;

  procedure receive_byte (
    signal sck      : out std_ulogic;
    signal sdio     : out std_ulogic;
    signal dut_sdio : in std_ulogic;
    signal dut_oe   : in std_ulogic;
    variable value  : out byte_t
  ) is
  begin

    sdio  <= '0';
    value := (others => '0');
    wait for 40 ns;
    assert dut_oe = '1'
      report "FPGA did not take control of SDIO"
      severity failure;

    for bit_index in 7 downto 0 loop

      sck              <= '1';
      wait for 20 ns;
      value(bit_index) := dut_sdio;
      wait for 20 ns;
      sck              <= '0';
      wait for 40 ns;

    end loop;

  end procedure receive_byte;

  procedure begin_transaction (
    signal cs_n : out std_ulogic
  ) is
  begin

    cs_n <= '0';
    wait for 80 ns;

  end procedure begin_transaction;

  procedure end_transaction (
    signal cs_n : out std_ulogic
  ) is
  begin

    cs_n <= '1';
    wait for 100 ns;

  end procedure end_transaction;

begin

  clk <= not clk after 5 ns;

  -- CLK_HZ => 1_000 makes the debouncer's window a single cycle, so the button press
  -- below registers immediately instead of after 320_000 cycles. It only reaches the
  -- debouncer; nothing else in the design derives timing from the generic, and in
  -- particular the SPI engine's behaviour is unaffected by it.
  dut : entity work.forgix_hello_world
    generic map (
      CLK_HZ => 1_000, DEBOUNCE_MS => 1
    )
    port map (
      clk_32m      => clk,
      spi_cs_n     => spi_cs_n,
      spi_sck      => spi_sck,
      spi_sdio_in  => spi_sdio_in,
      button_n     => button_n,
      spi_sdio_out => spi_sdio_out,
      spi_sdio_oe  => spi_sdio_oe,
      led_r_n      => led_r_n,
      led_g_n      => led_g_n,
      led_b_n      => led_b_n
    );

  stimulus : process is

    variable result       : byte_t;
    variable tick_a       : unsigned(31 downto 0);
    variable tick_b       : unsigned(31 downto 0);
    variable tick_c       : unsigned(31 downto 0);
    variable capture_a_at : time;
    variable capture_b_at : time;

    procedure ping (
      variable value : out byte_t
    ) is
    begin

      begin_transaction(spi_cs_n);
      send_read_request_byte(spi_sck, spi_sdio_in, spi_sdio_oe, CMD_PING);
      receive_byte(spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe, value);
      end_transaction(spi_cs_n);

    end procedure ping;

    procedure write_register (
      constant address, value : in byte_t
    ) is
    begin

      begin_transaction(spi_cs_n);
      send_byte(spi_sck, spi_sdio_in, CMD_WRITE);
      send_byte(spi_sck, spi_sdio_in, address);
      send_byte(spi_sck, spi_sdio_in, value);
      end_transaction(spi_cs_n);

    end procedure write_register;

    procedure read_register (
      constant address : in byte_t;
      variable value   : out byte_t
    ) is
    begin

      begin_transaction(spi_cs_n);
      send_byte(spi_sck, spi_sdio_in, CMD_READ);
      send_read_request_byte(spi_sck, spi_sdio_in, spi_sdio_oe, address);
      receive_byte(spi_sck, spi_sdio_in, spi_sdio_out, spi_sdio_oe, value);
      end_transaction(spi_cs_n);

    end procedure read_register;

    -- Assembles the 32-bit snapshot from its four byte registers. Deliberately no
    -- capture inside: keeping the latch a separate write is what lets the stability
    -- check below read twice and demand the same answer both times.

    procedure read_tick (
      variable value : out unsigned(31 downto 0)
    ) is

      variable tick_byte : byte_t;

    begin

      read_register(REG_TICK_0, tick_byte);
      value(7 downto 0)   := tick_byte;
      read_register(REG_TICK_1, tick_byte);
      value(15 downto 8)  := tick_byte;
      read_register(REG_TICK_2, tick_byte);
      value(23 downto 16) := tick_byte;
      read_register(REG_TICK_3, tick_byte);
      value(31 downto 24) := tick_byte;

    end procedure read_tick;

  begin

    spi_sck <= '0';
    -- Outlast the power-on reset before the first transaction. por counts to 0xFF, so
    -- rst holds for 256 clk_32m edges -- 2.56 us at this bench's 10 ns clock. Anything
    -- clocked in before that is swallowed by the reset and the ping below would fail
    -- for a reason that has nothing to do with the SPI engine.
    wait for 3 us;

    -- Ping first: it is the only exchange that proves the design is alive and is this
    -- design, so a failure here localizes to configuration rather than to the register
    -- semantics everything below depends on.
    ping(result);
    assert result = DESIGN_ID
      report "ping did not return the design ID"
      severity failure;

    write_register(REG_LED_R, x"12");
    write_register(REG_LED_G, x"34");
    write_register(REG_LED_B, x"56");
    write_register(REG_LED_GLOBAL, x"78");
    write_register(REG_LED_ENABLE, x"00");
    read_register(REG_LED_R, result);
    assert result = x"12"
      report "red register readback mismatch"
      severity failure;
    read_register(REG_LED_G, result);
    assert result = x"34"
      report "green register readback mismatch"
      severity failure;
    read_register(REG_LED_B, result);
    assert result = x"56"
      report "blue register readback mismatch"
      severity failure;
    read_register(REG_LED_GLOBAL, result);
    assert result = x"78"
      report "global brightness readback mismatch"
      severity failure;
    read_register(REG_LED_ENABLE, result);
    assert result = x"00"
      report "LED enable readback mismatch"
      severity failure;

    -- One clean press and release. 100 ns is 10 clk cycles, comfortably past the
    -- two-stage input synchronizer and the one-cycle debounce window this bench's
    -- CLK_HZ override produces, without being long enough to look like two presses.
    button_n <= '0';
    wait for 100 ns;
    button_n <= '1';
    wait for 100 ns;
    read_register(REG_BUTTON_COUNT, result);
    assert result = x"01"
      report "button press was not counted"
      severity failure;
    read_register(REG_STATUS, result);
    assert result(2) = '1'
      report "button event was not latched"
      severity failure;
    write_register(REG_BUTTON_COUNT, x"00");
    read_register(REG_BUTTON_COUNT, result);
    assert result = x"00"
      report "button count clear failed"
      severity failure;

    -- The tick counter's contract has three parts: a capture is atomic, reads do not
    -- re-sample, and the count advances at exactly one tick per clk cycle. The two
    -- captures below are byte-identical transactions, so the delay from each recorded
    -- now to the latch inside the write cancels, and every wait in between is a
    -- multiple of the bench's 10 ns clock period -- the delta assert can therefore
    -- demand exact equality, and 10 us is a thousand cycles, enough low-byte carries
    -- that a torn byte or a swapped lane cannot land on the right answer. The 134 s
    -- wrap is not simulated; the MCU's modular subtraction owns it and Ceedling
    -- covers that side.
    capture_a_at := now;
    write_register(REG_TICK_CAPTURE, x"00");
    read_tick(tick_a);
    write_register(REG_LED_R, x"77");
    read_tick(tick_b);
    assert tick_b = tick_a
      report "snapshot changed without a capture"
      severity failure;
    wait for 10 us;
    capture_b_at := now;
    write_register(REG_TICK_CAPTURE, x"00");
    read_tick(tick_b);
    assert tick_b - tick_a = (capture_b_at - capture_a_at) / 10 ns
      report "tick delta does not match elapsed clk cycles"
      severity failure;
    read_register(x"34", result);
    assert result = x"EE"
      report "address past the tick bytes should read as unmapped"
      severity failure;

    begin_transaction(spi_cs_n);
    send_byte(spi_sck, spi_sdio_in, CMD_RESET);
    end_transaction(spi_cs_n);
    read_register(REG_LED_B, result);
    assert result = x"20"
      report "reset did not restore blue default"
      severity failure;
    read_register(REG_LED_GLOBAL, result);
    assert result = x"40"
      report "reset did not restore brightness default"
      severity failure;
    read_register(REG_LED_ENABLE, result);
    assert result = x"01"
      report "reset did not enable LEDs"
      severity failure;
    -- CMD_RESET restored the register defaults above; the timebase must have kept
    -- running through it. A counter that cleared here would be living in
    -- register_file's reset arm, which is exactly the placement this assert forbids.
    write_register(REG_TICK_CAPTURE, x"00");
    read_tick(tick_c);
    assert tick_c > tick_b
      report "CMD_RESET cleared the free-running counter"
      severity failure;

    -- x"55" is not a command. The error bit it sets has to survive the end of this
    -- transaction and be readable by the separate one below -- a design that reported
    -- the error only for the duration of the offending exchange would be useless to a
    -- polling host and would pass every other check in this file.
    begin_transaction(spi_cs_n);
    send_byte(spi_sck, spi_sdio_in, x"55");
    end_transaction(spi_cs_n);
    read_register(REG_STATUS, result);
    assert result(7) = '1'
      report "invalid command did not set SPI error"
      severity failure;

    report "SPI register checks passed"
      severity note;
    stop;
    wait;

  end process stimulus;

end architecture sim;
