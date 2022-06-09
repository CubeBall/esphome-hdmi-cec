#ifdef USE_ARDUINO

#include "hdmi_cec.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hdmi_cec {

static const char *const TAG = "hdmi_cec";

void message_to_debug_string(char *message, const unsigned char *buffer, int count) {
  for (int i = 0; i < count; i++) {
    sprintf(&(message[i * 3]), "%02X", buffer[i]);
    if (i < count - 1) {
      sprintf(&(message[i * 3 + 2]), ":");
    }
  }
}

bool HdmiCec::LineState() {
  int state = this->pin_->digital_read();
  return state != LOW;
}

void HdmiCec::SetLineState(bool state) {
  if (state) {
    this->pin_->pin_mode(gpio::FLAG_INPUT);
  } else {
    this->pin_->digital_write(false);
    this->pin_->pin_mode(gpio::FLAG_OUTPUT);
  }
}

void HdmiCec::OnReady(int logical_address) {
  // This is called after the logical address has been allocated
  ESP_LOGD(TAG, "Device ready, Logical address assigned: %d", logical_address);
  this->address_ = logical_address;
  // Report physical address
  unsigned char buf[4] = {0x84, (unsigned char) (physical_address_ >> 8), (unsigned char) (physical_address_ & 0xff),
                          address_};
  this->send_data_internal_(this->address_, 0xF, buf, 4);
}

void HdmiCec::OnReceiveComplete(unsigned char *buffer, int count, bool ack) {
  // No command received?
  if (count < 2)
    return;

  auto source = (buffer[0] & 0xF0) >> 4;
  auto destination = (buffer[0] & 0x0F);

  // If we're not in promiscuous mode and the message isn't for us, ignore it.
  if (!this->promiscuous_mode_ && destination != this->address_ && destination != 0xF) {
    return;
  }

  // Pop the source/destination byte from buffer
  buffer = &buffer[1];
  count = count - 1;

  char debug_message[HDMI_CEC_MAX_DATA_LENGTH * 3];
  message_to_debug_string(debug_message, buffer, count);
  ESP_LOGD(TAG, "RX: (%d->%d) %02X:%s", source, destination, ((source & 0x0f) << 4) | (destination & 0x0f),
           debug_message);
  
  // unsigned char buf[9];

  if(destination == address_) {
    switch (buffer[0]) 
    {
      // Handling the physical address response in code instead of yaml since I think it always
      // needs to happen for other devices to be able to talk to this device.
      case 0x83: {
      // if (buffer[0] == 0x83 && destination == address_) {
        // Report physical address
        unsigned char buf[4] = {0x84, (unsigned char) (physical_address_ >> 8), (unsigned char) (physical_address_ & 0xff), address_};
        this->send_data_internal_(this->address_, 0xF, buf, 4);
        }
        break;
      //********************************
      // SUPPORT AV trafic
      // POWER STATUS - OFF
      case 0x8F: {
      // if (buffer[0] == 0x8F && destination == address_) {
        unsigned char buf[2] = {0x90, 0x01};
        this->send_data_internal_(this->address_, source, buf, 2);
      // }  
        break; }
      // OSD NAME
      case 0x46: {
      // if (buffer[0] == 0x46 && destination == address_) {
        unsigned char buf[9] = {0x47, 0x53, 0x6D, 0x61, 0x72, 0x74, 0x43, 0x45, 0x43}; //"SmartCEC"
        this->send_data_internal_(this->address_, source, buf, 9);
        }  
        break; 

      // Vendor data
      case 0x8C: {
      // if (buffer[0] == 0x8C && destination == address_) {
        unsigned char buf[4] = {0x87, 0x00, 0x00, 0x00}; //{0x87, 0x00, 0xE0, 0x36};
        this->send_data_internal_(this->address_, 0xF, buf, 4);
        }  
        break; 
      // CEC Version
      case 0x9F: {
      // if (buffer[0] == 0x9F && destination == address_) {
        unsigned char buf[2] = {0x9E, 0x05}; // Version 1.4
        this->send_data_internal_(this->address_, source, buf, 2);
        }  
        break; 
      // Give Deck status
      case 0x1A: {
      // if (buffer[0] == 0x1A && destination == address_) {
        unsigned char buf[2] = {0x1B, 0x1A}; // Deck Status - Stop
        this->send_data_internal_(this->address_, source, buf, 2);
        }  
        break;       
      // Deck control
      case 0x42: {
      // if (buffer[0] == 0x42 && destination == address_) {
        unsigned char buf[2] = {0x1B, 0x1A}; // Deck Status - Stop
        this->send_data_internal_(this->address_, source, buf, 2);
        }  
        break; 
      // Deck Play  
      case 0x41: {
      // if (buffer[0] == 0x41 && destination == address_) {
        unsigned char buf[2] = {0x1B, 0x1A}; // Deck Status - Stop
        this->send_data_internal_(this->address_, source, buf, 2);
        }  
        break; 
      // Vendor command with ID  
      case 0xA0: {
      // if (buffer[0] == 0xA0 && destination == address_) {
          if (source==0x05) {
            unsigned char buf[3] = {0x00, 0x00, 0x04}; // Abort - Refused
            this->send_data_internal_(this->address_, source, buf, 3);
          } else {
          unsigned char buf[3] = {0x00, 0x00, 0x03};  // Abort - Invalid operand
          this->send_data_internal_(this->address_, source, buf, 3);
          }
        }  
        break; 
      //********************************
      // default:
      //   //INVALID OPERAND
      // {  
      // unsigned char buf[3] = {0x00, 0x00, 0x03}; 
      //   this->send_data_internal_(this->address_, source, buf, 3);
      //   break;
      // }
    }
  }
  uint8_t opcode = buffer[0];
  for (auto *trigger : this->triggers_) {
    if ((!trigger->opcode_.has_value() || (*trigger->opcode_ == opcode)) &&
        (!trigger->source_.has_value() || (*trigger->source_ == source)) &&
        (!trigger->destination_.has_value() || (*trigger->destination_ == destination)) &&
        (!trigger->data_.has_value() ||
         (count == trigger->data_->size() && std::equal(trigger->data_->begin(), trigger->data_->end(), buffer)))) {
      auto data_vec = std::vector<uint8_t>(buffer, buffer + count);
      trigger->trigger(source, destination, data_vec);
    }
  }
}

void HdmiCec::OnTransmitComplete(unsigned char *buffer, int count, bool ack) {}

void IRAM_ATTR HOT HdmiCec::pin_interrupt(HdmiCec *arg) {
  if (arg->disable_line_interrupts_)
    return;
  arg->Run();
}

void HdmiCec::setup() {
  this->high_freq_.start();

  ESP_LOGCONFIG(TAG, "Setting up HDMI-CEC...");
  //this->Initialize(0x1300, CEC_Device::CDT_PLAYBACK_DEVICE, true);
  this->Initialize(this->physical_address_, CEC_Device::CDT_PLAYBACK_DEVICE, true);

  // This isn't quite enough to allow us to get rid of the HighFrequencyLoopRequester.
  // There's probably something that needs to wait a certain amount of time after
  // an interrupt.
  this->pin_->attach_interrupt(HdmiCec::pin_interrupt, this, gpio::INTERRUPT_ANY_EDGE);
}

void HdmiCec::dump_config() {
  ESP_LOGCONFIG(TAG, "HDMI-CEC:");
  ESP_LOGCONFIG(TAG, "  address: %d", this->address_);
  LOG_PIN("  Pin: ", this->pin_);
}

void HdmiCec::send_data(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data) {
  const uint8_t *buffer = reinterpret_cast<const uint8_t *>(data.data());
  auto *char_buffer = const_cast<unsigned char *>(buffer);

  this->send_data_internal_(source, destination, char_buffer, data.size());
}

void HdmiCec::send_data_internal_(uint8_t source, uint8_t destination, unsigned char *buffer, int count) {
  char debug_message[HDMI_CEC_MAX_DATA_LENGTH * 3];
  message_to_debug_string(debug_message, buffer, count);
  ESP_LOGD(TAG, "TX: (%d->%d) %02X:%s", source, destination, ((source & 0x0f) << 4) | (destination & 0x0f),
           debug_message);

  this->TransmitFrame(destination, buffer, count);
}

void HdmiCec::add_trigger(HdmiCecTrigger *trigger) { this->triggers_.push_back(trigger); };

void HdmiCec::loop() {
  this->disable_line_interrupts_ = true;
  this->Run();
  this->disable_line_interrupts_ = false;

  // The current implementation of CEC is inefficient and relies on polling to
  // identify signal changes at just the right time. Experimentally it needs to
  // run faster than every ~0.04ms to be reliable. This can be solved by creating
  // an interrupt-driven CEC driver.
  static int counter = 0;
  static int timer = 0;
  if (millis() - timer > 10000) {
    ESP_LOGD(TAG, "Ran %d times in 10000ms (every %fms)", counter, 10000.0f / (float) counter);
    counter = 0;
    timer = millis();
  }
  counter++;
}

}  // namespace hdmi_cec
}  // namespace esphome

#endif  // USE_ARDUINO
