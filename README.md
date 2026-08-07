# FlyingBytesPro

## Modern USBasp programming for classic AVR - GUI Avrdude Clone

AVR programming should feel fast, direct, and deliberate—not buried under old workflows.

**FlyingBytesPro** is a modern Windows desktop programmer built from the ground up for classic AVR microcontrollers. It combines a polished Qt interface, a direct USBasp backend, an AVRDUDE-style command line, and transparent low-level control in one focused application.

No wrapper. No unnecessary layers. Just your USBasp, your AVR, and the tools needed to make bytes fly.

## Why FlyingBytesPro

- **Supports 167 classic AVR microcontrollers through SPI ISP.**
- **Sophisticated and beautiful GUI with a simple, focused design.**
- **New AVRDUDE-style command-line interface using the USBasp protocol.**
- Automatic MCU detection using device-signature bytes.
- Completely rewritten backend for direct USBasp programming through libusb.
- Flash and EEPROM reading, writing, verification, and blank checking.
- Configurable automatic programming sequences and portable project files.
- Editable hexadecimal and ASCII memory buffers.
- Intel HEX and raw binary file loading and saving.
- Automatic SCK detection from 3 MHz down to 500 Hz.
- Fuse and lock-byte reading, masked programming, and readback verification.
- CRC-16 memory identification and detailed operation logging.

## Built for real AVR work

FlyingBytesPro turns the familiar USBasp into a complete AVR workstation. Inspect memory, edit bytes, detect targets, automate programming sequences, verify every important write, and keep the entire process visible.

It is designed for developers, hardware hackers, repair work, production experiments, vintage AVR projects, and anyone who still believes small microcontrollers deserve serious tools.

## Direct, fast, and transparent

FlyingBytesPro talks directly to USBasp through libusb. The graphical application and command-line programmer share the same purpose-built backend, so the experience stays consistent whether you prefer buttons, scripts, or automated workflows.

The result is a programmer that feels modern without hiding what the hardware is doing.

## Keep the bytes flying

FlyingBytesPro is built with stubborn attention to detail. If it saves you time, helps recover a board, or makes classic AVR development more enjoyable, consider buying me a coffee.

[![Buy Me a Coffee](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://paypal.me/flyandance?country.x=US&locale.x=en_US)
