#include <libusb.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

class CdcAcmUsbDevice {
public:
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
                if (buf[1] == (LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_DT_INTERFACE) && buf[2] == 2) {
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
            // Set line state

            ret = libusb_control_transfer(_dev_handle, 0x21, 0x22, 0x01 | 0x2, // DTR | RTS
                0, nullptr, 0, 0);
            if (ret != LIBUSB_SUCCESS) {
                throw std::runtime_error(libusb_strerror(ret));
            }

            // Set line encoding to 9600 8N1

            //        std::uint8_t enc[] = {0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08};
            //        ret = libusb_control_transfer(_dev_handle, 0x21, 0x20, 0, 0, enc,
            //                                      sizeof(enc), 0);
            //        if (ret != LIBUSB_SUCCESS) {
            //            throw std::runtime_error(libusb_strerror(ret));
            //        }
        }
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

            auto add_device = false;
            if ((pid == 0 && vid == 0) || (desc.idProduct == pid && desc.idVendor == vid)) {
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
                puts("\tSupported!");
                devices.emplace_back(std::make_shared<CdcAcmUsbDevice>(usb_dev, desc));
            } else {
                puts("\tNot supported.");
            }
        }

        return devices;
    }

private:
    libusb_context* _context;
    libusb_device** _dev_list;
    size_t _dev_count;
};

int main()
try {
    /* iceFUN uses a PIC16LF1459 to facilitate communication over USB (CDC-ACM)
       and to provide programming for the SPI flash memory */

    constexpr std::uint16_t PRODUCT_ID = 0xffee; // Devantech USB-ISS
    constexpr std::uint16_t VENDOR_ID = 0x04d8; // Microchip Technology Inc.

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

    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return EXIT_FAILURE;
} catch (...) {
    fprintf(stderr, "Unknown error %d\n", errno);
    return EXIT_FAILURE;
}
