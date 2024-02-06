# Batradio
Software defined radio on rpi zero

The goal of this project is to create a simple sdr using a rpi-zero and a simple hat.
It should work to up to 150kHz, to be used as a ULF radio or bat detector.

## Hat for raspberry Pi

### batradio_pcb
contains a KiCad project for a hat for the rpi (zero).
It contains a 1Mbit 16 bit spi adc, mcp33131 and a 14 bit dac, mcp4821.
Also a preamplifier and lowpass filter.

Status: This is the design of the prototype.
There still is one error in the PCB design. Pins 1 and 8 of the U4, a ths4521, should be swapped and a 100nF capacitor to ground should be connected to pin 2.

## Kernel module, a failed attempt.

Kernel module to process the data from adc and dac.
This also is work in progress.
Currently I have not been able to get the FIQ interrupt working.

### mcp331131_streaming

A variant of https://iosoft.blog/2020/11/16/streaming-analog-data-raspberry-pi/

The main changes for the 16 bit mcp33131 are:

  - Used PWM to create the CNVST pulse.
  - Writing dummy to create SCLK.
  - added -U option to send data over UDP channel.
  - DAC not used yet.
  
Basically works, but the generated SCLK is wrong and the data is partly wrong.

