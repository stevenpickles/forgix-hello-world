-- SPI target for the register protocol, in the shape the MCU bit-bangs it: chip select
-- frames a transaction, a command byte selects what follows, and reads turn the single
-- SDIO line around mid-transaction so the FPGA can answer on the same wire.
--
-- The important structural fact is that sck is not a clock here. Everything runs on
-- clk and sck is sampled like any other input, with edges recovered in the fabric.
-- That keeps the whole design in one clock domain and makes it trivially synthesizable,
-- but it imposes a rate limit the entity cannot express: sck must stay well below clk,
-- because each sck edge has to be several clk cycles wide to be seen at all. The MCU
-- bit-bangs at roughly 500 kHz against a 32 MHz clk, so the margin is about 64 cycles
-- per half period. A hardware SPI master pushed at clk/4 would silently drop bits.
--
-- Framing is enforced by cs_n rather than by counting: deasserting chip select at any
-- point abandons the transaction and returns to command_s, so a stalled or malformed
-- exchange cannot wedge the engine. `error` is sticky in the other direction -- it
-- survives to the next status read and is only cleared by CMD_RESET or rst -- because
-- the MCU polls it long after the offending transaction has ended.

library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use work.forgix_pkg.all;

entity forgix_spi is
  port (
    clk        : in    std_ulogic;
    rst        : in    std_ulogic;
    cs_n       : in    std_ulogic;
    sck        : in    std_ulogic;
    sdio_in    : in    std_ulogic;
    sdio_out   : out   std_ulogic;
    sdio_oe    : out   std_ulogic;
    reg_write  : out   std_ulogic;
    reg_read   : out   std_ulogic;
    reg_addr   : out   byte_t;
    reg_wdata  : out   byte_t;
    reg_rdata  : in    byte_t;
    reset_regs : out   std_ulogic;
    activity   : out   std_ulogic;
    error      : out   std_ulogic
  );
end entity forgix_spi;

architecture rtl of forgix_spi is

  -- read_wait_s and tx_arm_s are not part of the wire protocol; they are the two cycles
  -- of internal delay between the last address bit and the first reply bit. See the
  -- turnaround comment further down.

  type state_t is (command_s, address_s, data_s, read_wait_s, tx_arm_s, tx_s, done_s);

  -- Power-up state. These initializers are the FPGA's configuration-time values and
  -- matter beyond simulation tidiness: cs_sync starts all ones so a design that comes
  -- up with chip select already low is treated as idle until it sees a real assertion,
  -- rather than decoding whatever the line happened to be doing during configuration.
  signal state    : state_t                       := command_s;
  signal command  : byte_t                        := (others => '0');
  signal address  : byte_t                        := (others => '0');
  signal rx_shift : byte_t                        := (others => '0');
  signal tx_shift : byte_t                        := (others => '0');
  signal rx_count : natural range 0 to 7          := 0;
  signal tx_count : natural range 0 to 7          := 0;
  signal sck_sync : std_ulogic_vector(2 downto 0) := (others => '0');
  signal cs_sync  : std_ulogic_vector(2 downto 0) := (others => '1');
  signal io_sync  : std_ulogic_vector(2 downto 0) := (others => '0');
  signal oe       : std_ulogic                    := '0';

begin

  reg_addr <= address;
  sdio_oe  <= oe;

  spi_engine : process (clk) is

    variable received : byte_t;
    variable shifted  : byte_t;
    variable rise     : boolean;
    variable fall     : boolean;

  begin

    if rising_edge(clk) then
      -- Three stages, not the usual two. Two flops are enough to make a metastable
      -- sample settle; the third exists so that edge detection has a settled *pair* to
      -- compare. Bit 0 is the freshest sample and has been through one flop only, so
      -- it is exactly the bit that may still be resolving. Comparing bits 2 and 1
      -- means both operands of the comparison are twice-registered, and a metastable
      -- capture can no longer manufacture an sck edge that never happened.
      --
      -- Reading the settled end costs two clk cycles of latency on every edge, which
      -- is invisible at the ~64 cycles per sck half period this runs at.
      sck_sync   <= sck_sync(1 downto 0) & sck;
      cs_sync    <= cs_sync(1 downto 0) & cs_n;
      io_sync    <= io_sync(1 downto 0) & sdio_in;
      rise       := sck_sync(2 downto 1) = "01";
      fall       := sck_sync(2 downto 1) = "10";
      reg_write  <= '0';
      reg_read   <= '0';
      reset_regs <= '0';

      if rst = '1' then
        state    <= command_s;
        rx_count <= 0;
        tx_count <= 0;
        oe       <= '0';
        activity <= '0';
        error    <= '0';
        sdio_out <= '0';
      elsif cs_sync(2) = '1' then
        state    <= command_s;
        rx_count <= 0;
        tx_count <= 0;
        oe       <= '0';
        activity <= '0';
      else
        activity <= '1';

        -- One cycle spent letting the register file answer. reg_read was asserted on
        -- the cycle that entered this state, so reg_rdata is only valid now.
        if state = read_wait_s then
          tx_shift <= reg_rdata;
          tx_count <= 0;
          sdio_out <= reg_rdata(7);
          state    <= tx_arm_s;
        -- The turnaround. SDIO is one wire with two drivers, and this state exists
        -- purely to keep oe low across the remainder of the last address bit.
        --
        -- The MCU releases the line partway through that bit's high phase, after it
        -- has clocked the bit in (see _SendByte's releaseAfterSample in bsp_fpga.c).
        -- If the FPGA asserted oe as soon as the byte completed, both ends would be
        -- driving for the rest of that high phase. Waiting for the next falling edge
        -- puts the handover in the gap where neither end is clocking data, at the cost
        -- of half an sck period of latency.
        --
        -- tb_spi_regs pins both halves of this contract: it asserts oe is still low
        -- mid-high-phase ("FPGA drove SDIO before MCU turnaround") and that oe has
        -- risen after the falling edge. Removing this state passes nothing.
        elsif state = tx_arm_s and fall then
          oe    <= '1';
          state <= tx_s;
        elsif state = tx_s and fall then
          if tx_count = 7 then
            oe    <= '0';
            state <= done_s;
          else
            shifted  := tx_shift(6 downto 0) & '0';
            tx_shift <= shifted;
            sdio_out <= shifted(7);
            tx_count <= tx_count + 1;
          end if;
        end if;

        -- Receive is gated on oe so the engine does not clock in its own reply while
        -- it is driving the line.
        if oe = '0' and rise then
          -- io_sync(1), not io_sync(0) or io_sync(2), and for a reason beyond
          -- metastability: `rise` was derived from bits 2 and 1, so the sck sample
          -- being acted on entered the chain two clk cycles ago -- and the data
          -- captured on that same edge has been shifted exactly twice since, landing
          -- it in bit 1. Bit 0 would pair the edge with data two cycles younger, bit
          -- 2 with data a cycle older than the edge -- harmless at this sck rate,
          -- wrong in principle, and the first thing to break if the bus were ever
          -- sped up.
          received := rx_shift(6 downto 0) & io_sync(1);
          if rx_count = 7 then
            rx_shift <= received;
            rx_count <= 0;

            case state is

              when command_s =>

                command <= received;
                if received = CMD_PING then
                  tx_shift <= DESIGN_ID;
                  tx_count <= 0;
                  sdio_out <= DESIGN_ID(7);
                  state    <= tx_arm_s;
                elsif received = CMD_RESET then
                  reset_regs <= '1';
                  error      <= '0';
                  state      <= done_s;
                elsif received = CMD_WRITE or received = CMD_READ then
                  state <= address_s;
                else
                  error <= '1';
                  state <= done_s;
                end if;

              when address_s =>

                address <= received;
                if command = CMD_READ then
                  reg_read <= '1';
                  state    <= read_wait_s;
                else
                  state <= data_s;
                end if;

              when data_s =>

                reg_wdata <= received;
                reg_write <= '1';
                state     <= done_s;

              -- Reached when a byte arrives in done_s, i.e. the master kept clocking
              -- past the end of the transaction. Flagged rather than ignored because
              -- it means the two ends disagree about the protocol.
              when others =>

                error <= '1';
                state <= done_s;

            end case;

          else
            rx_shift <= received;
            rx_count <= rx_count + 1;
          end if;
        end if;
      end if;
    end if;

  end process spi_engine;

end architecture rtl;

