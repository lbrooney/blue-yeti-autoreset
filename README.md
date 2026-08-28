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

The udev rule accepts any serial number for that exact device and firmware. The
kernel-generated USB path becomes the systemd template instance. The service
reads the serial from that device's sysfs directory, and the reset binary checks
it again before the request is sent.

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
sudo udevadm control --reload-rules
```

The installation provides a static systemd template and a udev rule. It does
not need to be enabled. On the next boot or USB reconnection, udev starts the
appropriate instance automatically.

To process an already connected microphone immediately:

```sh
sudo udevadm trigger --action=add --subsystem-match=usb \
  --attr-match=idVendor=046d --attr-match=idProduct=0ab7
```

## How it works

1. `udev/70-blue-yeti-autoreset.rules` matches a USB device add event for the
   supported VID, PID, and firmware version.
2. The rule starts an instance such as `blue-yeti-autoreset@1-2.1.service` using
   the kernel-generated USB path, never untrusted descriptor text.
3. The service waits one second for enumeration and driver binding to settle.
4. The guard wrapper reads the serial from sysfs, records a timestamp under
   `/run`, and invokes `blue-yeti-reset` with that serial.
5. The binary verifies VID, PID, firmware version, and serial, temporarily
   detaches AudioControl interface 0, and claims it through libusb.
6. It sends one fixed extension-unit `SET_CUR` request:

```text
bmRequestType = 0x21
bRequest      = 0x01
wValue        = 0x0a00
wIndex        = 0x1900
wLength       = 8
payload       = 00 09 00 00 00 00 00 00
```

7. The firmware resets and the microphone re-enumerates.
8. The resulting second udev event consumes the fresh `/run` marker and exits
   without resetting again. A systemd start limit is an additional loop guard.

The `/run` marker is per USB path, is treated as stale after 15 seconds, and
never persists across reboot. A failed or interrupted reset removes its marker.

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

List service instances and inspect this boot's recovery logs:

```sh
systemctl list-units --all 'blue-yeti-autoreset@*'
journalctl -b -u 'blue-yeti-autoreset@*'
```

## Build and test

Development dependencies are make, libusb, pkgconf, a C17 compiler,
systemd/udev, and POSIX `sh`.

```sh
make
make check
```

`make check` builds with strict warnings, verifies plan-only mode, checks shell
syntax and loop-guard behavior, and validates the udev rule. It never sends a
live USB request.

## Limitations

- Recovery is proactive after USB enumeration; it does not continuously sample
  audio or diagnose unrelated capture failures.
- A reset briefly removes and recreates the ALSA and PipeWire devices.
- Firmware versions other than `0.20` are intentionally rejected because their
  reset handlers have not been verified.
- Automatic recovery requires systemd and udev.

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
