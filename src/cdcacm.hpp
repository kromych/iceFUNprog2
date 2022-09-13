#ifndef __CDC_ACM_HPP__
#define __CDC_ACM_HPP__

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

#include <libusb.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

class CdcAcmUsbDevice {
public:
    struct LineCoding {
        std::uint32_t bps;
        std::uint8_t stop_bits;
        std::uint8_t parity;
        std::uint8_t data_bits;
    } __attribute__((__packed__));

    CdcAcmUsbDevice(libusb_device* dev, libusb_device_descriptor desc)
        : _dev(dev)
        , _desc(desc)
    {
        int ret;

        if (_desc.bNumConfigurations != 1) {
            throw std::runtime_error("Number of configurations is not supported");
        }

        ret = libusb_open(_dev, &_dev_handle);
        if (ret != LIBUSB_SUCCESS) {
            throw std::runtime_error(libusb_strerror(ret));
        }

        ret = libusb_get_config_descriptor(_dev, 0, &_cfg);
        if (ret != LIBUSB_SUCCESS) {
            throw std::runtime_error(libusb_strerror(ret));
        }

        if (_cfg->bNumInterfaces != 2) {
            throw std::runtime_error("Number of interfaces is not supported");
        }

        // Make sure the device resembles a CDC-ACM one:
        //      * 1 configuration
        //      * 2 interfaces
        //      * the control interface has one IN endpoint
        //      * the data interface has one IN endpoint and one OUT endpoint

        const libusb_interface* ctrl_if {};
        const libusb_interface* data_if {};

        auto if0 = &_cfg->interface[0];
        auto if1 = &_cfg->interface[1];
        if (if0->num_altsetting != 1) {
            throw std::runtime_error("Number of altsettings is not supported");
        }
        if (if1->num_altsetting != 1) {
            throw std::runtime_error("Number of altsettings is not supported");
        }

        if (if0->altsetting[0].bNumEndpoints == 1) {
            ctrl_if = if0;
            data_if = if1;
        } else if (if1->altsetting[0].bNumEndpoints == 1) {
            ctrl_if = if1;
            data_if = if0;
        } else {
            throw std::runtime_error("Expected one control endpoint");
        }

        if (ctrl_if->altsetting[0].bInterfaceClass != LIBUSB_CLASS_COMM || ctrl_if->altsetting[0].bInterfaceSubClass != 2 || // ACM (modem)
            ctrl_if->altsetting[0].bInterfaceProtocol != 1) // AT-commands (v.25ter)
        {
            throw std::runtime_error("Control interface is not supported");
        }

        if (data_if->altsetting[0].bInterfaceClass != LIBUSB_CLASS_DATA || data_if->altsetting[0].bInterfaceSubClass != 0 || data_if->altsetting[0].bInterfaceProtocol != 0) {
            throw std::runtime_error("Data interface is not supported");
        }

        _ctrl_ep = ctrl_if->altsetting[0].endpoint;
        if ((_ctrl_ep->bEndpointAddress & 0x80) == 0) {
            throw std::runtime_error("Expected the IN control endpoint");
        }

        if (data_if->altsetting[0].bNumEndpoints != 2) {
            throw std::runtime_error("Number of data endpoints is not supported");
        }
        if ((data_if->altsetting[0].endpoint[0].bEndpointAddress & 0x80)
            && !(data_if->altsetting[0].endpoint[1].bEndpointAddress & 0x80)) {
            _data_in = data_if->altsetting[0].endpoint;
            _data_out = data_if->altsetting[0].endpoint + 1;
        } else if ((data_if->altsetting[0].endpoint[1].bEndpointAddress & 0x80)
            && !(data_if->altsetting[0].endpoint[0].bEndpointAddress & 0x80)) {
            _data_in = data_if->altsetting[0].endpoint + 1;
            _data_out = data_if->altsetting[0].endpoint;
        } else {
            throw std::runtime_error("Expected one IN data endpoint and one OUT data endpoint");
        }

        // Parse extra descriptor data looking for the ACM functional specification

        auto supports_line_state_encoding = false;
        if (ctrl_if->altsetting[0].extra_length) {
            auto size = ctrl_if->altsetting[0].extra_length;
            auto buf = ctrl_if->altsetting[0].extra;
            while (size >= 2) {
                // ACM functional description, there are many others
                if (buf[1] == ((std::uint8_t)LIBUSB_REQUEST_TYPE_CLASS | (std::uint8_t)LIBUSB_DT_INTERFACE) && buf[2] == 2) {
                    supports_line_state_encoding = buf[0] == 4 && // Length must be 4 bytes
                        (buf[3] & 0x02);
                    break;
                }

                size -= buf[0];
                buf += buf[0];
            }
        }

        // Claim interfaces

        for (auto if_idx = 0; if_idx < _cfg->bNumInterfaces; ++if_idx) {
            if (libusb_kernel_driver_active(_dev_handle, if_idx)) {
                libusb_detach_kernel_driver(_dev_handle, if_idx);
            }
            ret = libusb_claim_interface(_dev_handle, if_idx);
            if (ret != LIBUSB_SUCCESS) {
                throw std::runtime_error(libusb_strerror(ret));
            }
        }

        if (supports_line_state_encoding) {
            // Set line encoding

            auto coding = LineCoding {};
            ret = libusb_control_transfer(_dev_handle,
                0x21, // CDC ACM req type
                0x21, // GET_LINE_CODING
                0, 0,
                reinterpret_cast<std::uint8_t*>(&coding), sizeof(coding),
                5000);
            if (ret == LIBUSB_SUCCESS) {
                coding.bps = 3000000;

                ret = libusb_control_transfer(_dev_handle,
                    0x21, // CDC ACM req type
                    0x20, // SET_LINE_CODING
                    0, 0,
                    reinterpret_cast<std::uint8_t*>(&coding), sizeof(coding),
                    5000);
                if (ret != LIBUSB_SUCCESS) {
                    throw std::runtime_error(libusb_strerror(ret));
                }
            }

            // Set line state

            ret = libusb_control_transfer(_dev_handle,
                0x21, // CDC ACM req type
                0x22, // SET_CONTROL_LINE_STATE
                0x01 | 0x2, // DTR | RTS
                0, nullptr, 0, 5000);
            if (ret != LIBUSB_SUCCESS) {
                throw std::runtime_error(libusb_strerror(ret));
            }
        }
    }

    std::uint16_t write(const std::uint8_t* data, std::uint16_t size)
    {
        std::uint16_t sent_total = 0;

        while (sent_total < size) {
            int sent_this_time = 0;
            const auto to_send = std::min(_data_out->wMaxPacketSize, std::uint16_t(size - sent_total));

            if (libusb_bulk_transfer(_dev_handle,
                    _data_out->bEndpointAddress,
                    const_cast<std::uint8_t*>(&data[sent_total]),
                    to_send,
                    &sent_this_time, 5000)
                == LIBUSB_SUCCESS) {
                sent_total += sent_this_time;
            } else {
                break;
            }
        }

        return sent_total;
    }

    std::uint16_t read(std::uint8_t* data, std::uint16_t size)
    {
        std::uint16_t read_total = 0;

        while (read_total < size) {
            int read_this_time = 0;
            const auto to_read = std::min(_data_out->wMaxPacketSize, std::uint16_t(size - read_total));

            if (libusb_bulk_transfer(_dev_handle,
                    _data_in->bEndpointAddress,
                    &data[read_total],
                    to_read,
                    &read_this_time, 5000)
                == LIBUSB_SUCCESS) {
                read_total += read_this_time;
            } else {
                break;
            }
        }

        return read_total;
    }

    ~CdcAcmUsbDevice()
    {
        for (auto if_idx = 0; if_idx < _cfg->bNumInterfaces; ++if_idx) {
            libusb_release_interface(_dev_handle, if_idx);
            libusb_attach_kernel_driver(_dev_handle, if_idx);
        }

        libusb_free_config_descriptor(_cfg);
        _cfg = nullptr;
        libusb_close(_dev_handle);
        _dev = nullptr;
    }

private:
    libusb_device* _dev {};
    libusb_device_handle* _dev_handle {};
    libusb_config_descriptor* _cfg {};
    const libusb_endpoint_descriptor* _ctrl_ep {};
    const libusb_endpoint_descriptor* _data_in {};
    const libusb_endpoint_descriptor* _data_out {};
    libusb_device_descriptor _desc {};
};

class Usb {
public:
    Usb()
        : _context(nullptr)
        , _dev_list(nullptr)
        , _dev_count(0)
    {
        int ret;

        ret = libusb_init(&_context);
        if (ret != LIBUSB_SUCCESS) {
            throw std::runtime_error(libusb_strerror(ret));
        }
        // For the debug output form libusb:
        // libusb_set_option(_context,
        //                   libusb_option::LIBUSB_OPTION_LOG_LEVEL,
        //                   LIBUSB_LOG_LEVEL_DEBUG);
        // Can also set LIBUSB_DEBUG=4 in the environment.
    }

    ~Usb()
    {
        if (_dev_count) {
            close();
        }

        libusb_exit(_context);
        _context = nullptr;
    }

    void open()
    {
        if (_dev_count != 0) {
            throw std::runtime_error("Already opened");
        }

        auto ret = libusb_get_device_list(_context, &_dev_list);
        if (ret < LIBUSB_SUCCESS) {
            throw std::runtime_error(libusb_strerror(ret));
        }
        _dev_count = ret;
    }

    void close()
    {
        if (_dev_count == 0) {
            throw std::runtime_error("Not opened");
        }

        libusb_free_device_list(_dev_list, 1);

        _dev_list = nullptr;
        _dev_count = 0;
    }

    Usb(const Usb&) = delete;
    Usb& operator=(const Usb&) = delete;

    std::vector<std::shared_ptr<CdcAcmUsbDevice>> find(std::uint16_t vid = 0, std::uint16_t pid = 0)
    {
        int ret;
        libusb_device* usb_dev;
        std::vector<std::shared_ptr<CdcAcmUsbDevice>> devices;

        auto dev_idx = 0;
        while ((usb_dev = _dev_list[dev_idx++]) != nullptr) {
            libusb_device_descriptor desc {};

            ret = libusb_get_device_descriptor(usb_dev, &desc);
            if (ret != LIBUSB_SUCCESS) {
                throw std::runtime_error(libusb_strerror(ret));
            }

            auto add_device = false;
            if ((pid == 0 && vid == 0) || (desc.idProduct == pid && desc.idVendor == vid)) {
                {
                    libusb_device_handle* handle {};
                    if (libusb_open(usb_dev, &handle) == LIBUSB_SUCCESS) {
                        std::uint8_t vendor[256] {};
                        std::uint8_t product[256] {};
                        std::uint8_t serial[256] {};

                        libusb_get_string_descriptor_ascii(handle,
                            desc.iManufacturer,
                            vendor,
                            sizeof(vendor) - 1);
                        libusb_get_string_descriptor_ascii(handle,
                            desc.iProduct,
                            product,
                            sizeof(product) - 1);
                        libusb_get_string_descriptor_ascii(handle,
                            desc.iSerialNumber,
                            serial,
                            sizeof(serial) - 1);
                        fprintf(stdout,
                            "Device %#06x:%#06x @ (bus %03d, device %03d, vendor '%s', product '%s', serial '%s')\n",
                            desc.idVendor,
                            desc.idProduct,
                            libusb_get_bus_number(usb_dev),
                            libusb_get_device_address(usb_dev),
                            vendor,
                            product,
                            serial);

                        libusb_close(handle);
                    }
                }

                for (auto cfg_idx = 0; cfg_idx < desc.bNumConfigurations; ++cfg_idx) {
                    libusb_config_descriptor* cfg;

                    ret = libusb_get_config_descriptor(usb_dev, cfg_idx, &cfg);
                    if (ret != LIBUSB_SUCCESS) {
                        throw std::runtime_error(libusb_strerror(ret));
                    }

                    fprintf(stdout, "\tconfiguration %#02x\n", cfg_idx);

                    for (auto if_idx = 0; if_idx < cfg->bNumInterfaces; ++if_idx) {
                        const auto uif = &cfg->interface[if_idx];
                        for (auto intf_idx = 0; intf_idx < uif->num_altsetting;
                             ++intf_idx) {
                            const auto intf = &uif->altsetting[intf_idx];

                            fprintf(
                                stdout,
                                "\t\tinterface class:subclass:protocol %#04x:%#04x:%#04x\n",
                                intf->bInterfaceClass, intf->bInterfaceSubClass,
                                intf->bInterfaceProtocol);

                            add_device |= (intf->bInterfaceClass == LIBUSB_CLASS_COMM
                                && intf->bInterfaceSubClass == 2 && // ACM (modem)
                                intf->bInterfaceProtocol == 1); // AT-commands (v.25ter)
                        }
                    }

                    libusb_free_config_descriptor(cfg);
                }
            }

            if (add_device) {
                devices.emplace_back(std::make_shared<CdcAcmUsbDevice>(usb_dev, desc));
            }
        }

        return devices;
    }

private:
    libusb_context* _context;
    libusb_device** _dev_list;
    size_t _dev_count;
};

#endif
