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
      {"target", "source", "command", "value"});    // Register services with proper method pointers

    register_service(
      &PetFeederComponent::on_add_feeding_schedule,
      "add_feeding_schedule",
      {"hour", "minute", "portions"});

      register_service(
      &PetFeederComponent::on_clear_feeding_schedules,
      "clear_feeding_schedules"); 

    uint32_t hash = fnv1_hash("petfeeder_schedule_count");
    this->flash_schedules_count_ = global_preferences->make_preference<uint32_t>(hash, true);
    
    // Initialize fixed preference slots - do this during setup to ensure consistent order
    // ESP8266 does not support dynamic preference re-creation, and the order of creation matters
    for (size_t i = 0; i < MAX_FEEDING_SCHEDULES; i++) {
        uint32_t slot_hash = fnv1_hash("petfeeder_schedule_slot_" + std::to_string(i));
        this->rtc_schedule_slots_[i] = global_preferences->make_preference<uint32_t>(slot_hash, true);
        ESP_LOGD(TAG, "Created fixed schedule slot %d with hash %u", i, slot_hash);
    }
    
    this->load_schedules_();
}

void PetFeederComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pet Feeder Component:");
  this->check_uart_settings(9600);
  
  ESP_LOGCONFIG(TAG, "  Feeding Schedules:");
  for (size_t i = 0; i < this->feeding_schedules_.size(); i++) {
    auto schedule = this->feeding_schedules_[i];
    ESP_LOGCONFIG(TAG, "    Schedule %d: %02d:%02d - %d portions", i, schedule.hour, schedule.minute, schedule.portions);
  }

  this->load_schedules_();  // Load schedules to ensure they are up-to-date in the log
}

void PetFeederComponent::on_test_message(int target, int source, int command, int value) {

  if(target == 42){
    ESP_LOGI(TAG, "Restarting device");
    delay(100);  // NOLINT
    App.safe_reboot();
  }

  send_message_(target, source, command, {(char)value});
}

void PetFeederComponent::on_pet_feed(int portions) {
  ESP_LOGD(TAG, "Feeding %d portions", portions);
  
  // Spin the motor for portions
  send_message_(0x00, 0x06, 0x00, {0x65, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, (char)portions});
}

void PetFeederComponent::on_add_feeding_schedule(int hour, int minute, int portions) {
  ESP_LOGD(TAG, "Adding feeding schedule: %02d:%02d - %d portions", hour, minute, portions);
  
  // Validate input
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || portions <= 0 || portions > 10) {
    ESP_LOGW(TAG, "Invalid schedule parameters: hour=%d, minute=%d, portions=%d", hour, minute, portions);
    return;
  }

  // Check if we've reached the maximum number of schedules
  if (this->feeding_schedules_.size() >= MAX_FEEDING_SCHEDULES) {
    ESP_LOGW(TAG, "Cannot add schedule: maximum number of schedules (%d) reached, invoke clear first", MAX_FEEDING_SCHEDULES);
    return;
  }
  
  // Create a new schedule
  FeedingSchedule schedule;
  schedule.hour = hour;
  schedule.minute = minute;
  schedule.portions = portions;
  
  // Add to our schedules
  this->feeding_schedules_.push_back(schedule);
  
  // Save to flash
  this->save_schedules_();
  
  ESP_LOGD(TAG, "Feeding schedule added, now have %d schedules (max: %d)", 
           this->feeding_schedules_.size(), MAX_FEEDING_SCHEDULES);
}

void PetFeederComponent::on_clear_feeding_schedules() {
  ESP_LOGD(TAG, "Clearing all feeding schedules");
  this->feeding_schedules_.clear();
  this->save_schedules_();
}

void PetFeederComponent::save_schedules_() {
  // Save the count of schedules (up to MAX_FEEDING_SCHEDULES)
  uint32_t count = std::min(this->feeding_schedules_.size(), MAX_FEEDING_SCHEDULES);

  if (!this->flash_schedules_count_.save(&count)) {
    ESP_LOGW(TAG, "Failed to save schedule count to flash");
    return;
  }
  
  ESP_LOGD(TAG, "Saving %d feeding schedules to flash (max slots: %d)", count, MAX_FEEDING_SCHEDULES);
  
  // Save schedules into the fixed slots
  for (size_t i = 0; i < MAX_FEEDING_SCHEDULES; i++) {
    uint32_t schedule_data = 0;
    
    // Only save actual schedule data if we have a schedule for this slot
    if (i < this->feeding_schedules_.size()) {
      auto& schedule = this->feeding_schedules_[i];
      schedule_data = 
        (static_cast<uint32_t>(schedule.hour) << 16) | 
        (static_cast<uint32_t>(schedule.minute) << 8) | 
        static_cast<uint32_t>(schedule.portions);
        
      ESP_LOGD(TAG, "Saving schedule %d: %02d:%02d - %d portions (data: %08X)", 
               i, schedule.hour, schedule.minute, schedule.portions, schedule_data);
    } else {
      // For empty slots, save 0
      ESP_LOGD(TAG, "Clearing unused schedule slot %d", i);
    }
    
    if (!this->rtc_schedule_slots_[i].save(&schedule_data)) {
      ESP_LOGW(TAG, "Failed to save schedule %d to flash", i);
      continue;
    }
  }
  
  // Force preference system to commit all changes
  global_preferences->sync();
  
  ESP_LOGD(TAG, "Saved %d feeding schedules to flash", count);
}

void PetFeederComponent::load_schedules_() {
  this->feeding_schedules_.clear();
  
  // First load the count
  uint32_t count = 0;
  if (!this->flash_schedules_count_.load(&count)) {
    ESP_LOGD(TAG, "Failed to load schedule count from flash");
    return;
  }
  
  if (count == 0) {
    ESP_LOGD(TAG, "No feeding schedules found in flash (count = 0)");
    return;
  }
  
  // Sanity check - don't load more than the maximum number of schedules
  if (count > MAX_FEEDING_SCHEDULES) {
    ESP_LOGW(TAG, "Invalid schedule count in flash: %u, capping to %u", count, MAX_FEEDING_SCHEDULES);
    count = MAX_FEEDING_SCHEDULES;
  }
  
  ESP_LOGD(TAG, "Loading %d schedules from fixed slots", count);
  
  // Load schedules from fixed preference slots
  for (size_t i = 0; i < count; i++) {
    uint32_t schedule_data = 0;
    
    bool load_success = this->rtc_schedule_slots_[i].load(&schedule_data);
    
    if (load_success) {
      // Skip slots that are empty (0)
      if (schedule_data == 0) {
        ESP_LOGD(TAG, "Slot %d is empty, skipping", i);
        continue;
      }
      
      FeedingSchedule schedule;
      schedule.hour = (schedule_data >> 16) & 0xFF;
      schedule.minute = (schedule_data >> 8) & 0xFF;
      schedule.portions = schedule_data & 0xFF;
      
      ESP_LOGD(TAG, "Loaded from slot %d - Raw data: %08X, hour: %u, minute: %u, portions: %u",
               i, schedule_data, schedule.hour, schedule.minute, schedule.portions);
      
      // Validate data
      if (schedule.hour < 24 && schedule.minute < 60 && schedule.portions > 0 && schedule.portions < 100) {
        this->feeding_schedules_.push_back(schedule);
        ESP_LOGD(TAG, "Added schedule from slot %d: %02d:%02d - %d portions", 
                i, schedule.hour, schedule.minute, schedule.portions);
      } else {
        ESP_LOGW(TAG, "Invalid schedule data in slot %d: %08X", i, schedule_data);
      }
    } else {
      ESP_LOGW(TAG, "Failed to load schedule from slot %d", i);
    }
  }
  
  ESP_LOGD(TAG, "Loaded %d feeding schedules from flash", this->feeding_schedules_.size());
  for (size_t i = 0; i < this->feeding_schedules_.size(); i++) {
    ESP_LOGD(TAG, "  Schedule %d: %02d:%02d - %d portions", 
             i, this->feeding_schedules_[i].hour, this->feeding_schedules_[i].minute, 
             this->feeding_schedules_[i].portions);
  }
}

void PetFeederComponent::loop() {
  check_network_();
  check_feeding_schedules_();
  process_serial_();
}

void PetFeederComponent::check_feeding_schedules_() {
  // Check schedules every minute
  auto now = millis();
  if (now - this->last_schedule_check_ < 60000) {
    return;
  }
  
  this->last_schedule_check_ = now;
  
  if (this->feeding_schedules_.empty()) {
    return;
  }
  
  ESPTime current_time;
  
  // Use the time component if available, otherwise use system time
  if (this->time_ != nullptr) {
    current_time = this->time_->now();
  } else {
    current_time = ESPTime::from_epoch_local(::time(nullptr));
  }
  
  // If we don't have a valid time, we can't check schedules
  if (!current_time.is_valid()) {
    ESP_LOGD(TAG, "No valid time available, can't check feeding schedules");
    return;
  }
  
  // For each schedule, check if it's time to feed
  for (auto &schedule : this->feeding_schedules_) {
    // Check if the current time matches a schedule
    if (current_time.hour == schedule.hour && current_time.minute == schedule.minute) {
            ESP_LOGD(TAG, "It's feeding time! Schedule %02d:%02d - %d portions", 
               schedule.hour, schedule.minute, schedule.portions);
        // Fire an event to Home Assistant
      std::map<std::string, std::string> data;
      data["hour"] = std::to_string(schedule.hour);
      data["minute"] = std::to_string(schedule.minute);
      data["portions"] = std::to_string(schedule.portions);
      fire_homeassistant_event("esphome.petfeeder_auto_feeding", data);
      
      // Feed the pet
      this->on_pet_feed(schedule.portions);
    }
  }
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
  this->rtc_ = global_preferences->make_preference<int>(this->get_object_id_hash(), true);
  int saved_portion_count;
  if (!this->rtc_.load(&saved_portion_count))
    return {};
  return saved_portion_count;
}

}  // namespace petfeeder
}  // namespace esphome
