# Forgix has a 32 MHz oscillator connected to FPGA ball B4.
create_clock -period 31.250 -name clk_32m [get_ports {clk_32m}]

