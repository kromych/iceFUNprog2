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
krom@krom1p build % sudo ./iceFUNprog2 -r fw1.bin -o 0x40k -s 0x10k
Device 0x04d8:0xffee @ (bus 002, device 009, vendor 'Devantech Ltd.', product 'iceFUN', serial '00000000')
        configuration 00
                interface class:subclass:protocol 0x02:0x02:0x01
                interface class:subclass:protocol 0x0a:0000:0000
Board version: 1
Reset, flash ID: 0x1851f
Reading 16384 bytes starting at offset 65536 to 'fw1.bin'
................................................................
Saved 16384 bytes to 'fw1.bin'
Run: 00
```
