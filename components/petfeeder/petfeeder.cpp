#include "petfeeder.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace petfeeder {

static const char *const TAG = "petfeeder";

void PetFeederComponent::setup() {
   register_service(
      &PetFeederComponent::on_pet_feed,
      "feed_pet",
      {"portions"});
    
    register_service(
      &PetFeederComponent::on_test_message,
      "test_message",
      {"target", "source", "command", "value"});
}

void PetFeederComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pet Feeder Component:");
  this->check_uart_settings(9600);
}

void PetFeederComponent::on_test_message(int target, int source, int command, int value) {
  send_message_(target, source, command, {(char)value});
}

void PetFeederComponent::on_pet_feed(int portions) {
  ESP_LOGD(TAG, "Feeding %d portions", portions);
  
  // Spin the motor for portions
  send_message_(0x00, 0x06, 0x00, {0x65, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, (char)portions});
}

void PetFeederComponent::loop() {
  check_network_();
  process_serial_();
}

void PetFeederComponent::process_serial_() {
  while (available()) {
    char d = read();
    ESP_LOGD(TAG, "read %02X", d);

    if (this->incoming_message_.size() == 0) {
      if (d == 0x55) {
        this->incoming_message_.push_back(d);
      } else {
        ESP_LOGD(TAG, "Ignoring invalid data (%02X)", d);
      }
    } else {
      this->incoming_message_.push_back(d);

      // From MCU:
      // 0x55,    0xAA,    0x03,   0x07, 0x00,    0x08,   0x69, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x81
      // Header1, Header2, Target, Self, Command, Length, Data, Data, Data, Data, Data, Data, Data, Data, CHKSUM

      if (this->incoming_message_.size() >= 6) {
        if (this->incoming_message_[1] == 0xAA && this->incoming_message_[0] == 0x55) {
          auto target = this->incoming_message_[2];
          auto source = this->incoming_message_[3];
          auto command = this->incoming_message_[4];
          auto length = this->incoming_message_[5];

          if (this->incoming_message_.size() == 6 + length + 1) {
            auto sum = std::accumulate(this->incoming_message_.begin(), this->incoming_message_.end() - 1, (int)0);
            auto checksum = (char)(sum & 0xFF);

            if (checksum == this->incoming_message_.back()) {
              ESP_LOGD(TAG, "Got MCU frame target:%02X source:%02X command:%02X length:%02X", target, source, command, length);
              process_frame_(target, source, command, std::vector<char>(this->incoming_message_.begin()+6, this->incoming_message_.end() - 1));
            } else {
              ESP_LOGD(TAG, "Invalid MCU frame checksum (Expected %02X, got %02X)", checksum, this->incoming_message_.back());
            }

            this->incoming_message_.clear();
          } else {
            // Incomplete frame, continuing
          }
        } else {
          ESP_LOGD(TAG, "Invalid MCU frame, clearing");
          this->incoming_message_.clear();
        }
      }
    }
  }
}

void PetFeederComponent::process_frame_(char targetAddress, char sourceAddress, char command, std::vector<char> data) {
  if (targetAddress == 0x03) {
    if (sourceAddress == 0x07) {
      if (command == 0x00) {
        auto portions = data[7];
        ESP_LOGD(TAG, "MCU Ack %d portions", portions);
        if (this->counter_component_ != nullptr) {
          this->counter_component_->increment(portions);
        }
      }
    }
  }
}

bool PetFeederComponent::network_is_connected_() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr)
    return wifi::global_wifi_component->is_connected();
#endif
  return false;
}

void PetFeederComponent::check_network_() {
  auto now = micros();
  auto diff = now - this->last_update_;

  if (diff > 5*1000*1000) {
    this->last_update_ = now;

    if (network_is_connected_()) {
      if (!this->last_connected_) {
        this->last_connected_ = true;
        ESP_LOGD(TAG, "Network connected");

        // Steady light
        send_message_(0x00, 0x03, 0x00, {0x03});
      }
    } else {
      if (this->last_connected_) {
        this->last_connected_ = false;
        ESP_LOGD(TAG, "Network disconnected");

        // Slow blinking light
        send_message_(0x00, 0x03, 0x00, {0x02});
      }
    }
  }
}

void PetFeederComponent::send_message_(char targetAddress, char sourceAddress, char command, std::vector<char> data) {
  // 0x00, 0x06, 0x00, 0x08, 0x65, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x7A
  auto buffer = build_message_(targetAddress, sourceAddress, command, data);
  // Convert char vector to uint8_t vector for write_array
  std::vector<uint8_t> uint8_buffer(buffer.begin(), buffer.end());
  write_array(uint8_buffer);
}

std::vector<char> PetFeederComponent::build_message_(char targetAddress, char sourceAddress, char command, std::vector<char> data) {
  // From MCU:
  // 0x55,    0xAA,    0x03,   0x07, 0x00,    0x08,   0x69, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x81
  // Header1, Header2, Target, Self, Command, Length, Data, Data, Data, Data, Data, Data, Data, Data, CHKSUM

  // https://github.com/HuskyLens/HUSKYLENSArduino/blob/master/HUSKYLENS%20Protocol.md

  auto buffer = std::vector<char>();
  buffer.push_back(0x55);
  buffer.push_back(0xAA);
  buffer.push_back(targetAddress);
  buffer.push_back(sourceAddress);
  buffer.push_back(command);
  buffer.push_back((char)data.size());
  buffer.insert(buffer.end(), data.begin(), data.end());

  auto sum = std::accumulate(buffer.begin(), buffer.end(), (int)0);
  auto checksum = (char)(sum & 0xFF);

  buffer.push_back(checksum);

  return buffer;
}

// Counter Component Implementation

PetFeederPortionsCounterComponent::PetFeederPortionsCounterComponent() {
  ESP_LOGD(TAG, "PetFeederPortionsCounterComponent()");
  this->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
}

void PetFeederPortionsCounterComponent::setup() {
  this->current_portions_count_ = this->get_initial_state_().value_or(0);
  this->publish_state(this->current_portions_count_);
}

void PetFeederPortionsCounterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pet Feeder Portions Counter:");
  ESP_LOGCONFIG(TAG, "  Current Count: %d", this->current_portions_count_);
}

void PetFeederPortionsCounterComponent::increment(int count) {
  this->current_portions_count_ += count;
  this->publish_state(this->current_portions_count_);
  this->rtc_.save(&this->current_portions_count_);
}

optional<int> PetFeederPortionsCounterComponent::get_initial_state_() {
  this->rtc_ = global_preferences->make_preference<int>(this->get_object_id_hash());
  int saved_portion_count;
  if (!this->rtc_.load(&saved_portion_count))
    return {};
  return saved_portion_count;
}

}  // namespace petfeeder
}  // namespace esphome
