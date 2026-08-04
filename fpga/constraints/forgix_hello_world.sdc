# Forgix has a 32 MHz oscillator connected to FPGA ball B4.
create_clock -period 31.250 -name clk_32m [get_ports {clk_32m}]

# Every input is asynchronous to clk_32m -- the MCU bit-bangs SPI with no phase
# relationship to the oscillator, and the button is a human. All four land in
# multi-stage synchronizers in the fabric, so the crossings are declared and
# cut with set_false_path; the input delays exist so the pads carry a recorded
# budget instead of sitting unconstrained-and-silent. The port names are the
# core-side pins; the ISF maps spi_sdio_in/_out/_oe onto the one physical
# spi_sdio pad.
set_input_delay -clock clk_32m -max 10.000 [get_ports {spi_cs_n spi_sck spi_sdio_in button_n}]
set_input_delay -clock clk_32m -min 0.000 [get_ports {spi_cs_n spi_sck spi_sdio_in button_n}]
set_false_path -from [get_ports {spi_cs_n spi_sck spi_sdio_in button_n}]

# The outputs have no synchronous consumer either: the LEDs feed an eye, and
# the MCU samples SDIO roughly a microsecond after the driving edge of a bus
# it clocks itself. Constraining them to a same-cycle 10 ns budget fails the
# PWM's register-to-pad cone against a deadline nothing on the board imposes,
# so the paths are declared false after being given delays for the record.
set_output_delay -clock clk_32m -max 10.000 [get_ports {spi_sdio_out spi_sdio_oe led_r_n led_g_n led_b_n}]
set_output_delay -clock clk_32m -min 0.000 [get_ports {spi_sdio_out spi_sdio_oe led_r_n led_g_n led_b_n}]
set_false_path -to [get_ports {spi_sdio_out spi_sdio_oe led_r_n led_g_n led_b_n}]
