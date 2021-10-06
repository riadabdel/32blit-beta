#include "usb.hpp"

#include <cstring>

#include "tusb.h"

#include "config.h"
#include "file.hpp"
#include "storage.hpp"

#include "engine/api_private.hpp"

// msc
#ifndef USB_HOST
static bool storage_ejected = false;

void tud_mount_cb() {
  storage_ejected = false;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  (void) lun;

  const char vid[] = USB_VENDOR_STR;
  const char pid[] = USB_PRODUCT_STR " Storage";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if(storage_ejected) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
  }

  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void) lun;

  get_storage_size(*block_size, *block_count);
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void) lun;
  (void) power_condition;

  if(load_eject) {
    if (start) {
    } else
      storage_ejected = true;
  }

  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  (void) lun;

  return storage_read(lba, offset, buffer, bufsize);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void) lun;

  return storage_write(lba, offset, buffer, bufsize);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
  void const* response = NULL;
  uint16_t resplen = 0;

  switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      // Host is about to read/write etc ... better not to disconnect disk
      resplen = 0;
    break;

    default:
      // Set Sense = Invalid Command Operation
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

      // negative means error -> tinyusb could stall and/or response with failed status
      resplen = -1;
    break;
  }

  return resplen;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  return !get_files_open();
}
#endif

// cdc
#ifndef USB_HOST

static bool multiplayer_enabled = false;
static bool peer_connected = false;

static char cur_header[8];
static int header_pos = 0;

static uint16_t mp_buffer_len, mp_buffer_off;
static uint8_t *mp_buffer = nullptr;

static void send_all(const void *buffer, uint32_t len) {
  uint32_t done = tud_cdc_write(buffer, len);

  while(done < len) {
    tud_task();
    if(!tud_ready())
      break;

    done += tud_cdc_write((const char *)buffer + done, len - done);
  }
}

static void send_handshake(bool is_reply = false) {
  uint8_t val = 0;
  if(multiplayer_enabled)
    val = is_reply ? 2 : 1;

  uint8_t buf[]{'3', '2', 'B', 'L', 'M', 'L', 'T','I', val};
  send_all(buf, 9);
  tud_cdc_write_flush();
}
#endif

// hid
#if defined(USB_HOST) && defined(INPUT_USB_HID)

static int hid_report_id = -1;
static uint16_t buttons_offset = 0, num_buttons = 0;
static uint16_t hat_offset = 0, stick_offset = 0;

uint32_t hid_gamepad_id = 0;
uint8_t hid_joystick[2]{0x80, 0x80};
uint8_t hid_hat = 8;
uint32_t hid_buttons = 0;

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  printf("Mount %i %i, %04x:%04x\n", dev_addr, instance, vid, pid);

  hid_gamepad_id = (vid << 16) | pid;

  // basic and probably wrong report descriptor parsing
  auto desc_end = desc_report + desc_len;
  auto p = desc_report;

  int report_id = -1;
  int usage_page = -1;
  int usage = -1;
  int report_count = 0, report_size = 0;

  int bit_offset = 0;

  while(p != desc_end) {
    uint8_t b = *p++;

    int len = b & 0x3;
    int type = (b >> 2) & 0x3;
    int tag = b >> 4;

    if(type == RI_TYPE_MAIN) {
      // ignore constants
      if(tag == RI_MAIN_INPUT) {
        if(usage_page == HID_USAGE_PAGE_DESKTOP && usage == HID_USAGE_DESKTOP_X) {
          stick_offset = bit_offset;
          hid_report_id = report_id; // assume everything is in the same report as the stick... and that the first x/y is the stick
        } else if(usage_page == HID_USAGE_PAGE_DESKTOP && usage == HID_USAGE_DESKTOP_HAT_SWITCH) {
          hat_offset = bit_offset;
        } else if(usage_page == HID_USAGE_PAGE_BUTTON && !(*p & HID_CONSTANT)) {
          // assume this is "the buttons"
          buttons_offset = bit_offset;
          num_buttons = report_count;
        }

        usage = -1;
        bit_offset += report_size * report_count;
      } else if(tag == RI_MAIN_COLLECTION) {
        usage = -1; // check that this is gamepad?
      }
    } else if(type == RI_TYPE_GLOBAL) {
      if(tag == RI_GLOBAL_USAGE_PAGE)
        usage_page = *p;
      else if(tag == RI_GLOBAL_REPORT_SIZE)
        report_size = *p;
      else if(tag == RI_GLOBAL_REPORT_ID) {
        report_id = *p;
        bit_offset = 0;
      } else if(tag == RI_GLOBAL_REPORT_COUNT)
        report_count = *p;


    } else if(type == RI_TYPE_LOCAL) {
      if(tag == RI_LOCAL_USAGE && usage == -1)
        usage = *p; // FIXME: multiple usages are a thing
    }

    p += len;
  }

  if(!tuh_hid_receive_report(dev_addr, instance)) {
    printf("Cound not request report!\n");
  }
}

// should this be here or in input.cpp?
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {

  auto report_data = hid_report_id == -1 ? report : report + 1;

  // check report id if we have one
  if(hid_report_id == -1 || report[0] == hid_report_id) {
    // I hope these are reasonably aligned
    hid_hat = (report_data[hat_offset / 8] >> (hat_offset % 8)) & 0xF;

    hid_joystick[0] = report_data[stick_offset / 8];
    hid_joystick[1] = report_data[stick_offset / 8 + 1];

    // get up to 32 buttons
    hid_buttons = 0;
    int bits = buttons_offset % 8;
    int i = 0;
    auto p = report_data + buttons_offset / 8;

    // partial byte
    if(bits) {
      hid_buttons |= (*p++) >> bits;
      i += 8 - bits;
    }

    for(; i < num_buttons; i+= 8)
      hid_buttons |= (*p++) << i;
  }

  // next report
  tuh_hid_receive_report(dev_addr, instance);
}
#endif

void init_usb() {
  tusb_init();
}

void update_usb() {
#ifdef USB_HOST
  tuh_task();
#else // device
  tud_task();

  if(!tud_ready()) { // tud_cdc_connected returns false with STM USB host
    peer_connected = false;
  }

  while(tud_cdc_available()) {
    // match header
    if(header_pos < 8) {
      cur_header[header_pos] = tud_cdc_read_char();

      const char *expected = "32BL";
      if(header_pos >= 4 || cur_header[header_pos] == expected[header_pos])
        header_pos++;
      else
        header_pos = 0;
    } else {

      // get USER packet
      if(mp_buffer) {
        mp_buffer_off += tud_cdc_read(mp_buffer + mp_buffer_off, mp_buffer_len - mp_buffer_off);

        if(mp_buffer_off == mp_buffer_len) {
          if(blit::api.message_received)
            blit::api.message_received(mp_buffer, mp_buffer_len);

          delete[] mp_buffer;
          mp_buffer = nullptr;
          header_pos = 0;
        }
        continue;
      }

      // got header
      if(memcmp(cur_header + 4, "MLTI", 4) == 0) {
        // handshake packet
        peer_connected = tud_cdc_read_char() != 0;

        if(peer_connected)
          send_handshake(true);

        // done
        header_pos = 0;
      } else if(memcmp(cur_header + 4, "USER", 4) == 0) {
        if(tud_cdc_available() < 2)
          break;

        tud_cdc_read(&mp_buffer_len, 2);
        mp_buffer_off = 0;
        mp_buffer = new uint8_t[mp_buffer_len];

      } else {
        printf("got: %c%c%c%c%c%c%c%c\n", cur_header[0], cur_header[1], cur_header[2], cur_header[3], cur_header[4], cur_header[5], cur_header[6], cur_header[7]);
        header_pos = 0;
      }
    }
  }
#endif
}

void usb_debug(const char *message) {
#ifndef USB_HOST
  if(!tud_cdc_connected())
    return;

  auto len = strlen(message);
  send_all(message, len);
#endif
}

bool is_multiplayer_connected() {
#ifdef USB_HOST
  return false;
#else
  return multiplayer_enabled && peer_connected;
#endif
}

void set_multiplayer_enabled(bool enabled) {
#ifndef USB_HOST // could be supported with USB host, but we'd need a hub
  multiplayer_enabled = enabled;

  if(!enabled)
    send_handshake();
#endif
}

void send_multiplayer_message(const uint8_t *data, uint16_t len) {
#ifndef USB_HOST
  if(!peer_connected)
    return;

  uint8_t buf[]{'3', '2', 'B', 'L', 'U', 'S', 'E','R',
    uint8_t(len & 0xFF), uint8_t(len >> 8)
  };
  send_all(buf, 10);

  send_all((uint8_t *)data, len);

  tud_cdc_write_flush();
#endif
}
