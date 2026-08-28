#define _POSIX_C_SOURCE 200809L

#include <libusb.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    TARGET_VID = 0x046d,
    TARGET_PID = 0x0ab7,
    TARGET_BCD_DEVICE = 0x0020,
    XU_INDEX = 0x1900,
    COMMAND_VALUE = 0x0a00,
    TRANSFER_SIZE = 8,
    TIMEOUT_MS = 5000,
    AUDIO_CONTROL_INTERFACE = 0,
    DISCONNECT_POLL_COUNT = 50,
};

static const unsigned char ROM_RESET_COMMAND[TRANSFER_SIZE] = {
    0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--execute-reset --serial SERIAL]\n"
            "\n"
            "Without --execute-reset, print the fixed transaction and exit\n"
            "without initializing libusb. Live mode requires the exact serial.\n",
            program);
}

static void print_plan(bool execute)
{
    printf("Mode:       %s\n",
           execute ? "LIVE BLUE YETI RESET" : "PLAN ONLY (no USB access)");
    printf("Target:     %04x:%04x bcdDevice=%04x\n",
           TARGET_VID, TARGET_PID, TARGET_BCD_DEVICE);
    printf("SET_CUR:    bmRequestType=0x21 bRequest=0x01 "
           "wValue=0x%04x wIndex=0x%04x wLength=8\n",
           COMMAND_VALUE, XU_INDEX);
    printf("Payload:   ");
    for (size_t i = 0; i < TRANSFER_SIZE; ++i) {
        printf(" %02x", ROM_RESET_COMMAND[i]);
    }
    putchar('\n');
    printf("Effect:     volatile MCU/USB reset; expected disconnect and re-enumeration\n");
}

static libusb_device_handle *open_exact_device(libusb_context *context,
                                                const char *expected_serial)
{
    libusb_device **devices = NULL;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        fprintf(stderr, "libusb_get_device_list: %s\n", libusb_error_name((int)count));
        return NULL;
    }

    libusb_device_handle *match = NULL;
    for (ssize_t i = 0; i < count && match == NULL; ++i) {
        struct libusb_device_descriptor descriptor;
        int rc = libusb_get_device_descriptor(devices[i], &descriptor);
        if (rc < 0 || descriptor.idVendor != TARGET_VID ||
            descriptor.idProduct != TARGET_PID ||
            descriptor.bcdDevice != TARGET_BCD_DEVICE || descriptor.iSerialNumber == 0) {
            continue;
        }

        libusb_device_handle *candidate = NULL;
        rc = libusb_open(devices[i], &candidate);
        if (rc < 0) {
            fprintf(stderr, "Opening matching VID/PID failed: %s\n", libusb_error_name(rc));
            continue;
        }

        unsigned char serial[256] = {0};
        rc = libusb_get_string_descriptor_ascii(candidate, descriptor.iSerialNumber,
                                                 serial, (int)sizeof serial - 1);
        if (rc >= 0) {
            serial[rc] = '\0';
        }

        if (rc >= 0 && strcmp((const char *)serial, expected_serial) == 0) {
            match = candidate;
        } else {
            libusb_close(candidate);
        }
    }

    libusb_free_device_list(devices, 1);
    if (match == NULL) {
        fprintf(stderr, "No exact %04x:%04x bcdDevice=%04x serial=%s device found.\n",
                TARGET_VID, TARGET_PID, TARGET_BCD_DEVICE, expected_serial);
    }
    return match;
}

static void rebuild_audio_driver(libusb_device_handle *device, bool release_control,
                                 bool control_detached)
{
    bool detached_interfaces[3] = {control_detached, false, false};
    if (release_control) {
        const int rc = libusb_release_interface(device, AUDIO_CONTROL_INTERFACE);
        if (rc < 0) {
            fprintf(stderr, "Releasing interface 0 failed: %s\n", libusb_error_name(rc));
        }
    }

    for (int interface_number = 1; interface_number <= 2; ++interface_number) {
        int rc = libusb_kernel_driver_active(device, interface_number);
        if (rc == 1) {
            rc = libusb_detach_kernel_driver(device, interface_number);
            if (rc < 0) {
                fprintf(stderr, "Detaching interface %d failed: %s\n", interface_number,
                        libusb_error_name(rc));
            } else {
                detached_interfaces[interface_number] = true;
            }
        }
    }

    for (int interface_number = 0; interface_number <= 2; ++interface_number) {
        if (!detached_interfaces[interface_number]) {
            continue;
        }
        const int rc = libusb_attach_kernel_driver(device, interface_number);
        if (rc == LIBUSB_ERROR_BUSY &&
            libusb_kernel_driver_active(device, interface_number) == 1) {
            continue;
        }
        if (rc < 0) {
            fprintf(stderr, "Reattaching interface %d failed: %s\n", interface_number,
                    libusb_error_name(rc));
        }
    }
}

static int claim_audio_control(libusb_device_handle *device, bool *detached)
{
    int rc = libusb_kernel_driver_active(device, AUDIO_CONTROL_INTERFACE);
    if (rc == 1) {
        fprintf(stderr, "Temporarily detaching the kernel driver from interface 0.\n");
        rc = libusb_detach_kernel_driver(device, AUDIO_CONTROL_INTERFACE);
        if (rc < 0) {
            fprintf(stderr, "Detaching interface 0 failed: %s\n", libusb_error_name(rc));
            return rc;
        }
        *detached = true;
    } else if (rc < 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
        fprintf(stderr, "Checking interface 0 driver failed: %s\n", libusb_error_name(rc));
        return rc;
    }

    rc = libusb_claim_interface(device, AUDIO_CONTROL_INTERFACE);
    if (rc < 0) {
        fprintf(stderr, "Claiming interface 0 failed: %s\n", libusb_error_name(rc));
        if (*detached) {
            rebuild_audio_driver(device, false, true);
            *detached = false;
        }
    }
    return rc;
}

static bool wait_for_disconnect(libusb_device_handle *device)
{
    const struct timespec poll_delay = {.tv_sec = 0, .tv_nsec = 100000000L};
    int last_error = LIBUSB_SUCCESS;
    for (int attempt = 0; attempt < DISCONNECT_POLL_COUNT; ++attempt) {
        int configuration = 0;
        const int rc = libusb_get_configuration(device, &configuration);
        if (rc == LIBUSB_ERROR_NO_DEVICE) {
            return true;
        }
        if (rc < 0) {
            last_error = rc;
        }
        (void)nanosleep(&poll_delay, NULL);
    }
    if (last_error < 0) {
        fprintf(stderr, "No reset disconnect followed libusb error: %s\n",
                libusb_error_name(last_error));
    }
    return false;
}

int main(int argc, char **argv)
{
    bool execute = false;
    const char *expected_serial = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--execute-reset") == 0) {
            execute = true;
        } else if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            expected_serial = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    print_plan(execute);
    if (!execute) {
        return EXIT_SUCCESS;
    }
    if (expected_serial == NULL || expected_serial[0] == '\0') {
        fprintf(stderr, "Live mode requires --serial with the exact expected serial.\n");
        return EXIT_FAILURE;
    }
    fflush(stdout);

    libusb_context *context = NULL;
    int rc = libusb_init(&context);
    if (rc < 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(rc));
        return EXIT_FAILURE;
    }

    libusb_device_handle *device = open_exact_device(context, expected_serial);
    if (device == NULL) {
        libusb_exit(context);
        return EXIT_FAILURE;
    }

    bool detached = false;
    bool reset_accepted = false;
    rc = claim_audio_control(device, &detached);
    if (rc == LIBUSB_SUCCESS) {
        rc = libusb_control_transfer(device, 0x21, 0x01, COMMAND_VALUE, XU_INDEX,
                                     (unsigned char *)ROM_RESET_COMMAND,
                                     TRANSFER_SIZE, TIMEOUT_MS);
        if (rc == TRANSFER_SIZE) {
            reset_accepted = wait_for_disconnect(device);
            if (reset_accepted) {
                fprintf(stderr, "Reset request accepted; USB device disconnected.\n");
            } else {
                fprintf(stderr, "Reset request completed without a USB disconnect.\n");
                rebuild_audio_driver(device, true, detached);
            }
        } else {
            reset_accepted = rc == LIBUSB_ERROR_NO_DEVICE || wait_for_disconnect(device);
            if (reset_accepted) {
                fprintf(stderr, "USB device disconnected while accepting the reset.\n");
            } else {
                fprintf(stderr, "Reset SET_CUR failed: %s (%d)\n",
                        rc < 0 ? libusb_error_name(rc) : "short transfer", rc);
                rebuild_audio_driver(device, true, detached);
            }
        }
    }

    libusb_close(device);
    libusb_exit(context);
    return reset_accepted ? EXIT_SUCCESS : EXIT_FAILURE;
}
