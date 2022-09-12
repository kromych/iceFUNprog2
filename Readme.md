## iceFUNprog2

This project can program an iceFUNprog board amnd exists because I could not find anything
that would allow me to flash the board under MacOS or from the Windows command line.

Currently, the tool is able of:
* communicating with the board,
* resetting it,
* releasing it,
* getting the version of the board.

## Building

* Install a C++ compiler
* Install `libusb` package for your OS
* Install `cmake`

Now issue from the project root directory:
```sh
mkdir build
cd build
cmake ..
make
```
which should build `iceFUNprog2`.

You may need to run it with the administrative privileges:
```
krom@krom1p build % sudo ./iceFUNprog2
Device 0x04d8:0xffee @ (bus 002, device 011, vendor 'Devantech Ltd.', product 'iceFUN', serial '00000000')
	configuration 00
		interface class:subclass:protocol 0x02:0x02:0x01
		interface class:subclass:protocol 0x0a:0000:0000
	Supported!
Device 0x291a:0x8385 @ (bus 002, device 010, vendor 'Anker Innovations Limited', product 'Anker USB-C Hub Device ', serial '0000000000000001')
	Not supported.
Device 0x046d:0xc33c @ (bus 002, device 009, vendor 'Logitech', product 'G513 RGB MECHANICAL GAMING KEYBOARD', serial '166330573332')
	Not supported.
Device 0x0bda:0x48f0 @ (bus 002, device 008, vendor 'Generic', product 'USB Audio', serial '')
	Not supported.
Device 0x2109:0x2817 @ (bus 002, device 007, vendor 'VIA Labs, Inc.         ', product 'USB2.0 Hub             ', serial '')
	Not supported.
Device 0x2109:0x0817 @ (bus 002, device 006, vendor 'VIA Labs, Inc.         ', product 'USB3.0 Hub             ', serial '')
	Not supported.
Device 0x0bda:0x8153 @ (bus 002, device 005, vendor 'Realtek', product 'USB 10/100/1000 LAN', serial '000001')
	Not supported.
Device 0x14cd:0x8601 @ (bus 002, device 004, vendor 'USB Device  ', product 'USB 2.0 Hub            ', serial '')
	Not supported.
Device 0x05e3:0x0749 @ (bus 002, device 003, vendor 'Generic', product 'USB3.0 Card Reader', serial '000000001539')
	Not supported.
Device 0x2109:0x2817 @ (bus 002, device 002, vendor 'VIA Labs, Inc.         ', product 'USB2.0 Hub             ', serial '000000000')
	Not supported.
Device 0x2109:0x0817 @ (bus 002, device 001, vendor 'VIA Labs, Inc.         ', product 'USB3.0 Hub             ', serial '000000000')
	Not supported.
Board version: 1
Flash ID: 0x1851f
Run: 00
```
