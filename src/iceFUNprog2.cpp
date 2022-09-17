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

#include <fstream>

#include "cdcacm.hpp"
#include "cmdline.hpp"

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

std::uint8_t get_board_version(const std::shared_ptr<CdcAcmUsbDevice>& dev) {
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

std::uint32_t reset_board(const std::shared_ptr<CdcAcmUsbDevice>& dev) {
    const std::uint8_t reset = IceFunCommands::RESET_FPGA;
    std::uint32_t flash_id = 0;

    if (dev->write(&reset, sizeof(reset)) == sizeof(reset)) {
        if (dev->read(reinterpret_cast<std::uint8_t*>(&flash_id), 3) == 3) {
            return flash_id;
        }
    }

    throw std::runtime_error("Unable to reset the board");
}

std::uint8_t run_board(const std::shared_ptr<CdcAcmUsbDevice>& dev) {
    std::uint8_t run = IceFunCommands::RELEASE_FPGA;

    if (dev->write(&run, sizeof(run)) == sizeof(run)) {
        run = 0;
        dev->read(&run, sizeof(run));
    }

    return run;
}

void cycle_board(const std::shared_ptr<CdcAcmUsbDevice>& dev) {
    fprintf(stdout, "Cycling the board...\n");

    const auto board_version = get_board_version(dev);
    fprintf(stdout, "Board version: %d\n", board_version);

    const auto flash_id = reset_board(dev);
    fprintf(stdout, "Reset, flash ID: %#06x\n", flash_id);

    const auto run = run_board(dev);
    fprintf(stdout, "Run: %#02x\n", run);
}

void write_board(
    const std::shared_ptr<CdcAcmUsbDevice>& dev,
    std::optional<std::uint32_t> offset_opt,
    std::optional<std::uint32_t> size_opt,
    const std::string& path) {
    const std::uint32_t offset = offset_opt.value_or(0);
    if (offset > MAX_FLASH_SIZE) {
        throw std::runtime_error("The offset is too large");
    }

    ssize_t file_size = 0;

    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        throw std::runtime_error("Cannot open the file");
    }

    f.seekg(0, f.end);
    file_size = f.tellg();
    f.seekg(0, f.beg);

    const std::uint32_t size = size_opt.value_or(file_size);
    if (offset + size > MAX_FLASH_SIZE) {
        throw std::runtime_error("Cannot fit the data into the flash");
    }

    auto data = std::make_unique<char[]>(size);
    f.read(data.get(), size);

    const auto board_version = get_board_version(dev);
    fprintf(stdout, "Board version: %d\n", board_version);

    const auto flash_id = reset_board(dev);
    fprintf(stdout, "Reset, flash ID: %#06x\n", flash_id);

    const auto start_sector = (offset >> 16);
    const auto end_sector = ((offset + size) >> 16) + 1;

    fprintf(
        stdout,
        "Erasing %d 64k sectors starting at sector %d\n",
        end_sector - start_sector - 1,
        start_sector);
    for (auto sector_idx = start_sector; sector_idx < end_sector;
         ++sector_idx) {
        std::uint8_t erase[2] = {
            IceFunCommands::ERASE_64k,
            (std::uint8_t)sector_idx};

        if (dev->write(erase, sizeof(erase)) != sizeof(erase)) {
            throw std::runtime_error("Error when erasing sectors");
        }
        if (dev->read(erase, 1) != 1) {
            throw std::runtime_error(
                "Error when getting status for the erased sectors");
        }

        fprintf(stdout, ".");
    }
    fprintf(stdout, "\n");

    {
        fprintf(
            stdout,
            "Writing %d bytes starting at offset %d from '%s' to the flash\n",
            size,
            offset,
            path.c_str());
        std::uint32_t addr = offset;
        std::uint32_t written = 0;
        std::uint32_t end_addr = addr + size;
        while (addr < end_addr) {
            std::uint8_t write_data[260] {};
            std::uint8_t status[4] {};

            write_data[0] = IceFunCommands::PROG_PAGE;
            write_data[1] = (addr >> 16);
            write_data[2] = (addr >> 8);
            write_data[3] = addr;

            auto write_this_time =
                std::min((std::uint32_t)256, (std::uint32_t)(size - written));
            memcpy(write_data + 4, data.get() + written, write_this_time);

            dev->write(write_data, 4 + write_this_time);
            dev->read(status, sizeof(status));
            if (status[0] != 0) {
                fprintf(
                    stderr,
                    "\nError when writing, status: #%04x #%04x #%04x #%04x\n",
                    status[0],
                    status[1],
                    status[2],
                    status[3]);
                break;
            }
            fprintf(stdout, ".");

            written += write_this_time;
            addr += write_this_time;
        }
        fprintf(stdout, "\n");
        fprintf(stdout, "Wrote %u bytes\n", written);
    }

    {
        fprintf(
            stdout,
            "Verifying %d bytes starting at offset %d from '%s' to the flash\n",
            size,
            offset,
            path.c_str());
        std::uint32_t addr = offset;
        std::uint32_t verified = 0;
        std::uint32_t end_addr = addr + size;
        while (addr < end_addr) {
            std::uint8_t verify_data[260] {};
            std::uint8_t status[4] {};

            verify_data[0] = IceFunCommands::VERIFY_PAGE;
            verify_data[1] = (addr >> 16);
            verify_data[2] = (addr >> 8);
            verify_data[3] = addr;

            auto verified_this_time =
                std::min((std::uint32_t)256, (std::uint32_t)(size - verified));
            memcpy(verify_data + 4, data.get() + verified, verified_this_time);

            dev->write(verify_data, 4 + verified_this_time);
            dev->read(status, sizeof(status));
            if (status[0] != 0) {
                fprintf(
                    stderr,
                    "\nError when verifying, status: #%04x #%04x #%04x #%04x\n",
                    status[0],
                    status[1],
                    status[2],
                    status[3]);
                break;
            }
            fprintf(stdout, ".");

            verified += verified_this_time;
            addr += verified_this_time;
        }
        fprintf(stdout, "\n");
        fprintf(stdout, "Verified %u bytes\n", verified);
    }

    const auto run = run_board(dev);
    fprintf(stdout, "Run: %#02x\n", run);
}

void read_board(
    const std::shared_ptr<CdcAcmUsbDevice>& dev,
    std::optional<std::uint32_t> offset_opt,
    std::optional<std::uint32_t> size_opt,
    const std::string& path) {
    std::uint32_t offset = offset_opt.value_or(0);

    if (offset > MAX_FLASH_SIZE) {
        throw std::runtime_error("The offset is too large");
    }

    const std::uint32_t size = size_opt.value_or(MAX_FLASH_SIZE - offset);
    if (size > MAX_FLASH_SIZE) {
        throw std::runtime_error("The size is too large");
    }

    std::ofstream f(path, std::ios::out | std::ios::binary);
    if (!f) {
        throw std::runtime_error("Cannot open the file");
    }

    const auto board_version = get_board_version(dev);
    fprintf(stdout, "Board version: %d\n", board_version);

    const auto flash_id = reset_board(dev);
    fprintf(stdout, "Reset, flash ID: %#06x\n", flash_id);

    fprintf(
        stdout,
        "Reading %d bytes starting at offset %d to '%s'\n",
        size,
        offset,
        path.c_str());

    std::uint32_t read = 0;
    while (read < size) {
        char cmd_buf[260] {};

        cmd_buf[0] = IceFunCommands::READ_PAGE;
        cmd_buf[1] = offset >> 16;
        cmd_buf[2] = offset >> 8;
        cmd_buf[3] = offset;

        if (dev->write((std::uint8_t*)cmd_buf, 4) == 4) {
            if (dev->read((std::uint8_t*)cmd_buf + 4, 256) == 256) {
                f.write(cmd_buf + 4, 256);
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

    fprintf(stdout, "\n");
    fprintf(stdout, "Saved %d bytes to '%s'\n", read, path.c_str());

    const auto run = run_board(dev);
    fprintf(stdout, "Run: %#02x\n", run);
}

void disable_stdio_buffering() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

void usage(const char* prog_name) {
    fprintf(
        stderr,
        "Cross-platform programming tool for the Devantech iceFUN board.\n");
    fprintf(stderr, "Usage: %s <parameters> [options]\n", prog_name);
    fprintf(stderr, "Parameters:\n");
    fprintf(
        stderr,
        "  -h                display usage information and exit.\n");
    fprintf(stderr, "  -c                Cycle the board.\n");
    fprintf(
        stderr,
        "  -r <output file>  Save the contents of the on-board flash to the file.\n");
    fprintf(
        stderr,
        "  -w <input file>   Write the contents of the file to the on-board flash.\n");
    fprintf(stderr, "Options:\n");
    fprintf(
        stderr,
        "  -o <offset>       Optional start address for write or read (default: 0),\n");
    fprintf(
        stderr,
        "                    the suffix of 'k' signifies kibibytes, 'M' stands for mebibytes,\n");
    fprintf(
        stderr,
        "                    to use hexadecimals, prepend '0x' to the argument.\n");
    fprintf(
        stderr,
        "  -s <size>         Optional size to write or read, same syntax as for -o.\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s -w turing.bin\n", prog_name);
    fprintf(stderr, "  %s -r butterfly.bin -o 0x40k\n", prog_name);
    fprintf(stderr, "\n");
}

int main(int argc, char** argv) try {
    disable_stdio_buffering();

    const auto params = CommandLine(argc, argv);
    if (params.action == Action::UNKNOWN) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (params.action == Action::PRINT_USAGE) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    auto bus = Usb();
    bus.open();

    const auto devices = bus.find(params.vendor_id, params.product_id);
    if (devices.empty()) {
        throw std::runtime_error("No supported devices found");
    }
    if (devices.size() > 1) {
        throw std::runtime_error(
            "More than one supported device found. Please connect just one device");
    }

    const auto& dev = devices.front();
    if (params.action == Action::CYCLE_BOARD) {
        cycle_board(dev);
        return EXIT_SUCCESS;
    } else if (params.action == Action::READ_BOARD) {
        read_board(dev, params.offset, params.size, params.path);
        return EXIT_SUCCESS;
    } else if (params.action == Action::WRITE_BOARD) {
        write_board(dev, params.offset, params.size, params.path);
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
