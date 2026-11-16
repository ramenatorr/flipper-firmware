#pragma once

#include <stdbool.h>

/* USB side of the card: a serial command port that is always present, and a
 * drive mode that hands the whole microSD to the USB host.
 *
 * Drive mode is entered only by the serial command `mass`. It ends on the
 * serial command `card` (or `eject`), on a host eject, or when no USB host is
 * attached for 10 s. Ending it always reboots the card into its regular card
 * mode on the card that was active before (see usb_iface_boot_restore_card). */

/* Call once at boot, before settings/card setup: reads and clears the reboot flag. */
void usb_iface_boot_check(void);
/* True for the boot right after drive mode (or a serial `reboot`): restore the last card. */
bool usb_iface_boot_restore_card(void);

void usb_iface_init(void);
/* Service USB + serial commands. Call often from the core 0 loop. */
void usb_iface_task(void);
/* True once a host asked for drive mode; the card loop must stop and call usb_iface_run_mass(). */
bool usb_iface_mass_requested(void);
/* Runs drive mode until it ends, then reboots. Never returns. Card emulation must be stopped. */
void usb_iface_run_mass(void) __attribute__((noreturn));
