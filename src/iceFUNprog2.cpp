/*
    Copyright (C) 2022 kromych <kromych@users.noreply.github.com>

    Permission to use, copy, modify, and/or distribute this software for any purpose with or
    without fee is hereby granted, provided that the above copyright notice and
    this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
    THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
    AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
    CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "cdcacm.hpp"
#include <cstring>

constexpr std::uint32_t MAX_FLASH_SIZE = 1048576;

enum IceFunCommands : std::uint8_t {
    DONE = 0xb0,
    GET_VER,
    RESET_FPGA,
    ERASE_CHIP,
    ERASE_64k,
    PROG_PAGE,
    READ_PAGE,
    VERIFY_PAGE,
    GET_CDONE,
    RELEASE_FPGA
};

std::uint8_t get_board_version(const std::shared_ptr<CdcAcmUsbDevice>& dev)
{
    const std::uint8_t get_ver = IceFunCommands::GET_VER;
    std::uint8_t ver[2] {};

    if (dev->write(&get_ver, sizeof(get_ver)) == sizeof(get_ver)) {
        if (dev->read(ver, sizeof(ver)) == sizeof(ver)) {
            if (ver[0] == 38) {
                return ver[1];
            }
        }
    }

    throw std::runtime_error("Unable to get board version");
}

std::uint32_t reset_board(const std::shared_ptr<CdcAcmUsbDevice>& dev)
{
    const std::uint8_t reset = IceFunCommands::RESET_FPGA;
    std::uint32_t flash_id = 0;

    if (dev->write(&reset, sizeof(reset)) == sizeof(reset)) {
        if (dev->read(reinterpret_cast<std::uint8_t*>(&flash_id), 3) == 3) {
            return flash_id;
        }
    }

    throw std::runtime_error("Unable to reset the board");
}

std::uint8_t run_board(const std::shared_ptr<CdcAcmUsbDevice>& dev)
{
    std::uint8_t run = IceFunCommands::RELEASE_FPGA;

    if (dev->write(&run, sizeof(run)) == sizeof(run)) {
        run = 0;
        dev->read(&run, sizeof(run));
    }

    return run;
}

void cycle_board(const std::shared_ptr<CdcAcmUsbDevice>& dev)
{
    fprintf(stdout, "Cycling the board...\n");

    const auto board_version = get_board_version(dev);
    fprintf(stdout, "Board version: %d\n", board_version);

    const auto flash_id = reset_board(dev);
    fprintf(stdout, "Reset, flash ID: %#06x\n", flash_id);

    const auto run = run_board(dev);
    fprintf(stdout, "Run: %#02x\n", run);
}

void write_board(const std::shared_ptr<CdcAcmUsbDevice>& dev,
    std::uint32_t offset,
    const char* path)
{
}

void read_board(const std::shared_ptr<CdcAcmUsbDevice>& dev,
    std::uint32_t offset,
    const char* path)
{
    std::uint32_t size = MAX_FLASH_SIZE;

    auto f = fopen(path, "wb");
    if (f == nullptr) {
        throw std::runtime_error("Cannot open the file");
    }

    const auto board_version = get_board_version(dev);
    fprintf(stdout, "Board version: %d\n", board_version);

    const auto flash_id = reset_board(dev);
    fprintf(stdout, "Reset, flash ID: %#06x\n", flash_id);

    fprintf(stdout, "Reading %d bytes starting at offset %d to '%s'\n",
        size, offset, path);

    std::uint32_t read = 0;
    while (read < size) {
        std::uint8_t cmd_buf[260] {};

        cmd_buf[0] = IceFunCommands::READ_PAGE;
        cmd_buf[1] = offset >> 16;
        cmd_buf[2] = offset >> 8;
        cmd_buf[3] = offset;

        if (dev->write(cmd_buf, 4) == 4) {
            if (dev->read(cmd_buf + 4, 256) == 256) {
                fwrite(cmd_buf + 4, 256, 1, f);
            } else {
                break;
            }
        } else {
            break;
        }

        fprintf(stdout, ".");

        read += 256;
        offset += 256;
    }

    fclose(f);

    fprintf(stdout, "\n");
    fprintf(stdout, "Saved %d bytes to '%s'\n", read, path);

    const auto run = run_board(dev);
    fprintf(stdout, "Run: %#02x\n", run);
}

void disable_stdio_buffering()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void usage(const char* prog_name)
{
    fprintf(stderr, "Cross-platform programming tool for the Devantech iceFUN board.\n");
    fprintf(stderr, "Usage: %s <options>\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h                display usage information and exit.\n");
    fprintf(stderr, "  -o <offset>       start address for write or read (default: 0),\n");
    fprintf(stderr, "                    sufix of 'k' signifies kibibytes, 'M' stands for mebibytes,\n");
    fprintf(stderr, "                    to use hexadecimals, prepend '0x' to the argument.\n");
    fprintf(stderr, "  -r <output file>  Save the contents of the on-board flash to the file.\n");
    fprintf(stderr, "  -w <input file>   Write the contents of the file to the on-board flash.\n");
    fprintf(stderr, "  -c                Cycle the board.\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s -w turing.bin\n", prog_name);
    fprintf(stderr, "  %s -r butterfly.bin -o 0x40k\n", prog_name);
    fprintf(stderr, "\n");
}

enum class Action {
    UNKNOWN,
    PRINT_USAGE,
    CYCLE_BOARD,
    READ_BOARD,
    WRITE_BOARD
};

int main(int argc, char** argv)
try {
    /* iceFUN uses a PIC16LF1459 to facilitate communication over USB (CDC-ACM)
       and to provide programming for the SPI flash memory */

    constexpr std::uint16_t PRODUCT_ID = 0xffee; // Devantech USB-ISS
    constexpr std::uint16_t VENDOR_ID = 0x04d8; // Microchip Technology Inc.

    char path[] = "fw.bin";
    std::uint32_t offset = 0;
    auto action = Action::UNKNOWN;

    disable_stdio_buffering();

    if (argc == 2) {
        if (!strcmp(argv[1], "-h")) {
            action = Action::PRINT_USAGE;
        } else if (!strcmp(argv[1], "-c")) {
            action = Action::CYCLE_BOARD;
        }
    }

    if (action == Action::UNKNOWN) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (action == Action::PRINT_USAGE) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    auto bus = Usb();
    bus.open();

    auto devices = bus.find(VENDOR_ID, PRODUCT_ID);
    if (devices.empty()) {
        throw std::runtime_error("No supported devices found");
    }
    if (devices.size() > 1) {
        throw std::runtime_error(
            "More than one supported device found. Please connect just one device");
    }

    auto dev = devices.front();
    if (action == Action::CYCLE_BOARD) {
        cycle_board(dev);
        return EXIT_SUCCESS;
    } else if (action == Action::READ_BOARD) {
        read_board(dev, offset, path);
        return EXIT_SUCCESS;
    } else if (action == Action::WRITE_BOARD) {
        write_board(dev, offset, path);
        return EXIT_SUCCESS;
    }

    throw std::logic_error("Unsupported option");
} catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return EXIT_FAILURE;
} catch (...) {
    fprintf(stderr, "Unknown error %d\n", errno);
    return EXIT_FAILURE;
}
