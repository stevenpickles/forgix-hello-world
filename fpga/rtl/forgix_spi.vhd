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

  type state_t is (command_s, address_s, data_s, read_wait_s, tx_arm_s, tx_s, done_s);

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

        if state = read_wait_s then
          tx_shift <= reg_rdata;
          tx_count <= 0;
          sdio_out <= reg_rdata(7);
          state    <= tx_arm_s;
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

        if oe = '0' and rise then
          received := rx_shift(6 downto 0) & io_sync(2);
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

