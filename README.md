# blue-yeti-autoreset

`blue-yeti-autoreset` is a Linux recovery utility for a Blue Yeti Classic that is
detected normally by USB, ALSA, and PipeWire but stops delivering capture
samples. Recording applications can open the microphone and ALSA can report a
running 44.1 or 48 kHz stream while its hardware pointer remains at zero. The
most visible symptom is no microphone capture after boot even though the Yeti
remained connected and appears ready to use. The same fault can be reproduced
by some logical USB hub power cycles. It cannot be repaired by restarting
PipeWire, rebinding `snd-usb-audio`, or issuing a host USB reset.

The traditional workaround is to physically unplug and reconnect the Yeti, or
otherwise remove USB power long enough to give it a complete power cycle. That
works because the microphone performs its full cold-start initialization when
power returns. This utility provides the equivalent recovery automatically,
without requiring access to the cable after every affected boot.

The utility automatically recovers the microphone by sending the same volatile
ROM-reset command used by the official control software. The command resets the
microphone's MCU and USB logic, causes a clean
disconnect/re-enumeration, and restores capture without flashing firmware or
writing persistent settings.

## Supported device

- USB vendor/product: `046d:0ab7`
- Firmware (`bcdDevice`): `0.20` (`0020` in sysfs)
- USB Audio Class: UAC1
- Known affected production era: 2020-2023

At boot, the service accepts any serial number for that exact device and
firmware. It reads each matching serial from sysfs, and the reset binary checks
the device identity and serial again before the request is sent.

This utility is intended for 2020-2023-era Blue Yeti Classic CR units using this
hardware and firmware. Blue reused the Yeti name across revisions, so the year
or enclosure alone is not a compatibility check; the USB ID and firmware value
above are authoritative.

## Install on Arch Linux

Install the `blue-yeti-autoreset` package from the AUR with an AUR helper:

```sh
yay -S blue-yeti-autoreset
```

For development installs from this checkout:

```sh
make check
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable --now blue-yeti-autoreset.service
```

The installation provides a systemd service that must be enabled once. It runs
once during each boot after initial udev device processing has settled. USB
reconnections later in the same boot do not trigger another automatic reset.

To enable automatic recovery starting with the next boot without resetting an
already connected microphone immediately:

```sh
sudo systemctl enable blue-yeti-autoreset.service
```

## How it works

1. `blue-yeti-autoreset.service` starts once during boot after initial udev
   processing has settled.
2. The wrapper scans sysfs for every device with the supported VID, PID,
   firmware version, and a non-empty serial, then invokes `blue-yeti-reset` for
   each serial.
3. The binary verifies VID, PID, firmware version, and serial, temporarily
   detaches AudioControl interface 0, and claims it through libusb.
4. It sends one fixed extension-unit `SET_CUR` request:

```text
bmRequestType = 0x21
bRequest      = 0x01
wValue        = 0x0a00
wIndex        = 0x1900
wLength       = 8
payload       = 00 09 00 00 00 00 00 00
```

5. The firmware resets and the microphone re-enumerates. No hotplug rule starts
   the service again, and `RemainAfterExit` keeps the successful service active
   for the rest of the boot.

## Manual recovery

Without arguments, the binary prints its exact transaction and does not access
USB:

```sh
blue-yeti-reset
```

An intentional live reset requires both the action flag and device serial:

```sh
sudo blue-yeti-reset --execute-reset --serial SERIAL
```

There is no option to select another opcode or supply an arbitrary payload.

## Logs

Inspect the service state and this boot's recovery logs:

```sh
systemctl status blue-yeti-autoreset.service
journalctl -b -u blue-yeti-autoreset.service
```

## Build and test

Development dependencies are make, libusb, pkgconf, a C17 compiler, systemd,
and POSIX `sh`.

```sh
make
make check
```

`make check` builds with strict warnings, verifies plan-only mode, checks shell
syntax and boot-time device discovery, and validates the systemd unit. It never
sends a live USB request.

## Limitations

- Recovery is proactive once at boot; it does not react to later USB
  reconnections, continuously sample audio, or diagnose unrelated capture
  failures.
- A reset briefly removes and recreates the ALSA and PipeWire devices.
- Firmware versions other than `0.20` are intentionally rejected because their
  reset handlers have not been verified.
- Automatic recovery requires systemd.

## Cause of the bug

When the microphone remains powered across a host boot, firmware 0.20 can
complete its USB bus-reset handler and audio setup while a deeper capture domain
remains powered or insufficiently reset. The handler does call the normal audio
initialization routine, but it does not restore the hardware to the same state
as a cold start. USB endpoints return, the microphone accepts the streaming
interface, and ALSA reports a running stream, but the hardware pointer never
advances. Firmware opcode `0x09` resets the MCU and that deeper device state, as
a full USB power cycle does. The exact surviving ADC, PLL, clock, codec, or
stream-engine state has not yet been isolated.

## License

MIT
