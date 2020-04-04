# Batradio
Software defined radio on rpi zero

The goal of this project is to create a simple sdr using a rpi-zero and a simple hat.
It should work to up to 200kHz, to be used as a radio and a bat detector.

## Hat for raspberry Pi

batradio contains a KiCad project for a hat for the rpi (zero).
It contains a 1Mbit 16 bit spi adc, mcp31331 and a 14 bit dac.
Also a preamplifier and lowpass filter.

### Status:

This is the design of the prototype.
There still is one error in this design. Pins 1 and 8 should be swapped and, eventually, a 100nF capacitor to ground should be connected to pin 2.



## Kernel module

Kernel module to process the data from adc and dac.
