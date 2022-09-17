#ifndef __CMD_LINE_HPP__
#define __CMD_LINE_HPP__

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

#include <cstring>
#include <optional>
#include <string>

enum class Action {
    UNKNOWN,
    PRINT_USAGE,
    CYCLE_BOARD,
    READ_BOARD,
    WRITE_BOARD
};

struct CommandLine {
    CommandLine(int argc, char** argv) {
        auto argi = 1;
        while (argi < argc) {
            if (!strcmp(argv[argi], "-h")) {
                if (argc == 2) {
                    action = Action::PRINT_USAGE;
                    break;
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else if (!strcmp(argv[argi], "-c")) {
                if (argc == 2) {
                    action = Action::CYCLE_BOARD;
                    break;
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else if (!strcmp(argv[argi], "-r")) {
                if (action == Action::UNKNOWN && path.empty()) {
                    ++argi;
                    if (argi < argc) {
                        action = Action::READ_BOARD;
                        path = argv[argi];
                    } else {
                        action = Action::UNKNOWN;
                        break;
                    }
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else if (!strcmp(argv[argi], "-w")) {
                if (action == Action::UNKNOWN && path.empty()) {
                    ++argi;
                    if (argi < argc) {
                        action = Action::WRITE_BOARD;
                        path = argv[argi];
                    } else {
                        action = Action::UNKNOWN;
                        break;
                    }
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else if (!strcmp(argv[argi], "-o")) {
                if (!offset.has_value()) {
                    ++argi;
                    if (argi < argc) {
                        char* end_ptr = argv[argi];
                        std::uint32_t off = 0;

                        off = strtoul(argv[argi], &end_ptr, 0);
                        if (*end_ptr == '\0') {
                            offset = std::make_optional(off);
                        } else if (!strcmp(end_ptr, "k")) {
                            offset = std::make_optional(off * 1024);
                        } else if (!strcmp(end_ptr, "M")) {
                            offset = std::make_optional(off * 1024 * 1024);
                        } else {
                            action = Action::UNKNOWN;
                            break;
                        }
                    } else {
                        action = Action::UNKNOWN;
                        break;
                    }
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else if (!strcmp(argv[argi], "-s")) {
                if (!size.has_value()) {
                    ++argi;
                    if (argi < argc) {
                        char* end_ptr = argv[argi];
                        std::uint32_t sz = 0;

                        sz = strtoul(argv[argi], &end_ptr, 0);
                        if (*end_ptr == '\0') {
                            size = std::make_optional(sz);
                        } else if (!strcmp(end_ptr, "k")) {
                            size = std::make_optional(sz * 1024);
                        } else if (!strcmp(end_ptr, "M")) {
                            size = std::make_optional(sz * 1024 * 1024);
                        } else {
                            action = Action::UNKNOWN;
                            break;
                        }
                    } else {
                        action = Action::UNKNOWN;
                        break;
                    }
                } else {
                    action = Action::UNKNOWN;
                    break;
                }
            } else {
                action = Action::UNKNOWN;
                break;
            }

            ++argi;
        }
    }

    // iceFUN uses a Microchip PIC16LF1459 to facilitate communication over USB (CDC-ACM)
    // and to provide programming for the SPI flash memory (Kynix AT25SF081).
    std::uint16_t product_id {0xffee};  // Devantech USB-ISS
    std::uint16_t vendor_id {0x04d8};  // Microchip Technology Inc.
    Action action {Action::UNKNOWN};
    std::string path;
    std::optional<std::uint32_t> offset;
    std::optional<std::uint32_t> size;
};

#endif
