/* Serial commands + USB drive mode (retrobox fork).
 *
 * Serial (CDC) commands, one per line, each answered with one line starting
 * with "OK" or "ERR":
 *   version            firmware version
 *   status             "OK mode=card card=<folder> chan=<n>" or "OK mode=mass"
 *   mass               enter drive mode (the microSD appears as a USB drive)
 *   card | eject       leave drive mode: reboot into card mode on the previous card
 *   reboot             reboot into card mode on the current card
 *   bootloader         reboot into the RP2040 USB bootloader (RPI-RP2 drive)
 *   help
 * Leaving drive mode never depends on the host ejecting the drive: the host
 * unmounts it, then sends `card`. A host eject also ends it. */

#include "usb_iface.h"

#if !DEBUG_USB_UART

#include <stdio.h>
#include <string.h>

#include "gc.h"
#include "gc_cardman.h"
#include "hardware/watchdog.h"
#include "oled.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "sd.h"
#include "tusb.h"
#include "version/version.h"

#define REBOOT_MAGIC 0x464D5242u /* "FMRB" */
#define REBOOT_RESTORE_CARD 1u
#define NO_HOST_TIMEOUT_US (10u * 1000u * 1000u)
#define EXIT_DELAY_US (200u * 1000u)

static bool restore_card_on_boot;
static volatile bool mass_requested;
static volatile bool mass_active;
static bool exit_pending;
static uint64_t exit_at_us;
static uint32_t sectors;
static uint32_t n_reads, n_writes, n_read_fail, n_write_fail, n_retries_ok;
static uint32_t first_fail_lba = 0xFFFFFFFFu;

static char line[64];
static size_t line_len;

void usb_iface_boot_check(void) {
    if (watchdog_hw->scratch[0] == REBOOT_MAGIC && watchdog_hw->scratch[1] == REBOOT_RESTORE_CARD)
        restore_card_on_boot = true;
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
}

bool usb_iface_boot_restore_card(void) {
    return restore_card_on_boot;
}

static void __attribute__((noreturn)) reboot_into_card_mode(void) {
    watchdog_hw->scratch[0] = REBOOT_MAGIC;
    watchdog_hw->scratch[1] = REBOOT_RESTORE_CARD;
    watchdog_reboot(0, 0, 0);
    while (1)
        tight_loop_contents();
}

static void reply(const char *s) {
    if (!tud_cdc_connected())
        return;
    tud_cdc_write_str(s);
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}

/* Keep USB serviced for a moment so the last reply reaches the host. */
static void drain(uint32_t ms) {
    uint64_t end = time_us_64() + (uint64_t)ms * 1000u;
    while (time_us_64() < end) {
        tud_task();
        tud_cdc_write_flush();
    }
}

static void request_exit(void) {
    if (!exit_pending) {
        exit_pending = true;
        exit_at_us = time_us_64() + EXIT_DELAY_US;
    }
}

static void handle_command(char *cmd) {
    char buf[160];

    if (!strcmp(cmd, "version")) {
        snprintf(buf, sizeof(buf), "OK flippermce %s retrobox-usb", flippermce_version);
        reply(buf);
    } else if (!strcmp(cmd, "status")) {
        if (mass_active) {
            snprintf(buf, sizeof(buf), "OK mode=mass reads=%lu writes=%lu rfail=%lu wfail=%lu retried=%lu sderr=0x%04lx first_fail=%ld",
                     (unsigned long)n_reads, (unsigned long)n_writes, (unsigned long)n_read_fail,
                     (unsigned long)n_write_fail, (unsigned long)n_retries_ok, (unsigned long)sd_last_error(),
                     first_fail_lba == 0xFFFFFFFFu ? -1L : (long)first_fail_lba);
            reply(buf);
        }
        else if (mass_requested)
            reply("OK mode=entering-mass");
        else {
            snprintf(buf, sizeof(buf), "OK mode=card card=%s chan=%d",
                     gc_cardman_get_folder_name(), gc_cardman_get_channel());
            reply(buf);
        }
    } else if (!strcmp(cmd, "mass") || !strcmp(cmd, "set mode mass")) {
        if (mass_active || mass_requested) {
            reply("OK mass already");
        } else {
            mass_requested = true;
            reply("OK mass");
        }
    } else if (!strcmp(cmd, "card") || !strcmp(cmd, "eject") || !strcmp(cmd, "set mode card")) {
        if (mass_active) {
            if (!sd_sync_cache()) {
                reply("ERR sd sync failed");
                return;
            }
            reply("OK card");
            request_exit();
        } else {
            reply("OK card already");
        }
    } else if (!strcmp(cmd, "reboot")) {
        reply("OK reboot");
        drain(100);
        if (mass_active)
            sd_sync_cache();
        reboot_into_card_mode();
    } else if (!strcmp(cmd, "bootloader")) {
        reply("OK bootloader");
        drain(100);
        if (mass_active)
            sd_sync_cache();
        reset_usb_boot(0, 0);
    } else if (!strcmp(cmd, "help")) {
        reply("OK commands: version status mass card eject reboot bootloader");
    } else if (cmd[0]) {
        reply("ERR unknown command");
    }
}

static void poll_serial(void) {
    while (tud_cdc_available()) {
        int c = tud_cdc_read_char();
        if (c < 0)
            break;
        if (c == '\r' || c == '\n') {
            line[line_len] = 0;
            handle_command(line);
            line_len = 0;
        } else if (line_len < sizeof(line) - 1) {
            line[line_len++] = (char)c;
        }
    }
}

void usb_iface_init(void) {
    tusb_init();
}

void usb_iface_task(void) {
    tud_task();
    poll_serial();
}

bool usb_iface_mass_requested(void) {
    return mass_requested;
}

static void show_drive_screen(void) {
    oled_clear();
    oled_text_home();
    oled_draw_text("USB DRIVE MODE\n\nSD card on USB\n'card' to exit");
    oled_show();
}

void usb_iface_run_mass(void) {
    /* Card emulation is stopped and its image flushed; hand the SD to USB. */
    if (!sd_close_all()) {
        reply("ERR sd handover failed");
        drain(100);
        reboot_into_card_mode();
    }
    sectors = sd_sector_count();
    if (!sectors) {
        reply("ERR sd size unknown");
        drain(100);
        reboot_into_card_mode();
    }
    show_drive_screen();
    mass_active = true;
    mass_requested = false;

    uint64_t last_host_us = time_us_64();
    while (1) {
        usb_iface_task();
        uint64_t now = time_us_64();
        if (tud_mounted())
            last_host_us = now;
        else if (now - last_host_us > NO_HOST_TIMEOUT_US)
            request_exit();
        if (exit_pending && now >= exit_at_us)
            break;
    }
    sd_sync_cache();
    drain(50);
    reboot_into_card_mode();
}

/* ---- MSC: the whole microSD as one LUN, only while drive mode runs ---- */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "FlipMCE ", 8);
    memcpy(product_id, "SD card         ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (mass_active && !exit_pending)
        return true;
    /* Card mode: a reader with no medium. */
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = mass_active ? sectors : 0;
    *block_size = 512;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

static int32_t transfer(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *bytes, uint32_t size, bool write) {
    static uint8_t sector[512];
    if (!tud_msc_test_unit_ready_cb(lun))
        return -1;
    uint64_t pos = (uint64_t)lba * 512u + offset;
    uint64_t total = (uint64_t)sectors * 512u;
    if (pos > total || size > total - pos) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }
    uint32_t remaining = size;
    while (remaining) {
        uint32_t block = (uint32_t)(pos / 512u), off = (uint32_t)(pos % 512u);
        uint32_t n = 512u - off;
        if (n > remaining)
            n = remaining;
        if (!write || off || n != 512u) {
            uint32_t before = sd_last_error();
            ++n_reads;
            if (!sd_read_sector_robust(block, sector)) {
                ++n_read_fail;
                if (first_fail_lba == 0xFFFFFFFFu)
                    first_fail_lba = block;
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0C : 0x11, 0x00);
                return -1;
            }
            if (sd_last_error() != before)
                ++n_retries_ok;
        }
        if (write) {
            memcpy(sector + off, bytes, n);
            ++n_writes;
            if (!sd_write_sector_robust(block, sector)) {
                ++n_write_fail;
                if (first_fail_lba == 0xFFFFFFFFu)
                    first_fail_lba = block;
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
                return -1;
            }
        } else {
            memcpy(bytes, sector + off, n);
        }
        remaining -= n;
        bytes += n;
        pos += n;
    }
    return (int32_t)size;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    return transfer(lun, lba, offset, (uint8_t *)buffer, bufsize, false);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    return transfer(lun, lba, offset, buffer, bufsize, true);
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (start || !load_eject)
        return true;
    /* Host eject: commit everything, then leave drive mode after the reply is out. */
    if (mass_active) {
        if (!sd_sync_cache()) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
            return false;
        }
        request_exit();
    }
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
        case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
            return 0;
        case 0x35: /* SYNCHRONIZE CACHE (10) */
            if (!mass_active || sd_sync_cache())
                return 0;
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
            return -1;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}

#else /* DEBUG_USB_UART: the SDK's stdio USB owns TinyUSB; no drive mode in debug builds. */

#include "hardware/watchdog.h"

static bool restore_card_on_boot;

void usb_iface_boot_check(void) {
    if (watchdog_hw->scratch[0] == 0x464D5242u && watchdog_hw->scratch[1] == 1u)
        restore_card_on_boot = true;
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
}
bool usb_iface_boot_restore_card(void) { return restore_card_on_boot; }
void usb_iface_init(void) {}
void usb_iface_task(void) {}
bool usb_iface_mass_requested(void) { return false; }
void usb_iface_run_mass(void) { watchdog_reboot(0, 0, 0); while (1) {} }

#endif
