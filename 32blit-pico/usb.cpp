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

static bool is_switch = false;

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  printf("Mount %i %i, %04x:%04x\n", dev_addr, instance, vid, pid);

  is_switch = (vid == 0x20d6 && pid == 0xa711); //PowerA wired pro controller

  // TODO parse report descriptor

  if(!tuh_hid_receive_report(dev_addr, instance)) {
    printf("Cound not request report!\n");
  }
}

// should this be here or in input.cpp?
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  auto protocol = tuh_hid_interface_protocol(dev_addr, instance);

  // hat -> dpad
  const uint32_t dpad_map[]{
    blit::Button::DPAD_UP,
    blit::Button::DPAD_UP | blit::Button::DPAD_RIGHT,
    blit::Button::DPAD_RIGHT,
    blit::Button::DPAD_DOWN | blit::Button::DPAD_RIGHT,
    blit::Button::DPAD_DOWN,
    blit::Button::DPAD_DOWN | blit::Button::DPAD_LEFT,
    blit::Button::DPAD_LEFT,
    blit::Button::DPAD_UP | blit::Button::DPAD_LEFT,
    0
  };

  uint8_t joystick_x = 0x80, joystick_y = 0x80;

  uint32_t buttons = 0;

  if(is_switch) {
    // Y,  B,  A,  X,  L,  R, ZL, ZR
    // -,  +, LS, RS, Ho, SS
    joystick_x = report[3];
    joystick_y = report[4];

    int dpad = std::min(8, report[2] & 0xF);
    buttons = dpad_map[dpad];

    const uint8_t button_map[]{
       2,  4, // A
       1,  5, // B
       3,  6, // X
       0,  7, // Y
       8,  8, // MENU (mapped to -)
      12,  9, // HOME
      10, 10 // JOYSTICK
    };

    for(int i = 0; i < sizeof(button_map); i += 2) {
      int byte = button_map[i] >>  3;
      int bit = button_map[i] & 7;
      if(report[byte] & (1 << bit))
        buttons |= (1 << button_map[i + 1]);
    }

  } else if(report[0] == 1) {
    // layout roughly matches the PS4 report from the controller example, but the mapping is for my Razer Raiju Mobile
    joystick_x = report[1];
    joystick_y = report[2];
    // 3 = z, 4 =rotation

    // DPAD         ,  A,  B,  ?,  X
    // Y,  ?, L1, R1, L2, R2, Se, St
    // ?, LS, RS, Ho, Ba

    const uint8_t button_map[]{
      44,  4, // A
      45,  5, // B
      47,  6, // X
      48,  7, // Y
      60,  8, // MENU (mapped to Back)
      59,  9, // HOME
      57, 10 // JOYSTICK
    };

    buttons = dpad_map[report[5] & 0xF];

    for(int i = 0; i < sizeof(button_map); i += 2) {
      int byte = button_map[i] >>  3;
      int bit = button_map[i] & 7;
      if(report[byte] & (1 << bit))
        buttons |= (1 << button_map[i + 1]);
    }
  }

  blit::api.buttons = buttons;
  blit::api.joystick.x = (float(joystick_x) - 0x80) / 0x80;
  blit::api.joystick.y = (float(joystick_y) - 0x80) / 0x80;

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
