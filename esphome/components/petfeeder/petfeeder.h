#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/components/time/real_time_clock.h"
#include <numeric>
#include <vector>
#include <time.h>
#include "esphome/core/application.h"
#include "esphome/components/api/custom_api_device.h"

namespace esphome {
namespace petfeeder {

struct FeedingSchedule {
  uint8_t hour;
  uint8_t minute;
  uint8_t portions;
};

// Forward declaration
class PetFeederPortionsCounterComponent;

class PetFeederComponent : public Component, public uart::UARTDevice, public api::CustomAPIDevice
{
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  
  void set_counter_component(PetFeederPortionsCounterComponent *counter) { this->counter_component_ = counter; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  
  void on_pet_feed(int portions);
  void on_test_message(int target, int source, int command, int value);
  void on_set_feeding_schedule(std::vector<FeedingSchedule> schedules);
  void on_clear_feeding_schedules();
 protected:
  PetFeederPortionsCounterComponent *counter_component_{nullptr};
  time::RealTimeClock *time_{nullptr};
  bool last_connected_{false};
  uint32_t last_update_{0};
  uint32_t last_schedule_check_{0};
  std::vector<char> incoming_message_{};
  std::vector<FeedingSchedule> feeding_schedules_{};
  ESPPreferenceObject rtc_schedules_;
  void process_serial_();
  void process_frame_(char targetAddress, char sourceAddress, char command, std::vector<char> data);
  void check_network_();
  void check_feeding_schedules_();
  void send_message_(char targetAddress, char sourceAddress, char command, std::vector<char> data);
  std::vector<char> build_message_(char targetAddress, char sourceAddress, char command, std::vector<char> data);
  
  bool network_is_connected_();
  void save_schedules_();
  void load_schedules_();
};

class PetFeederPortionsCounterComponent : public sensor::Sensor, public Component {
 public:
  PetFeederPortionsCounterComponent();
  void setup() override;
  void dump_config() override;
  
  void increment(int count);
  
 protected:
  ESPPreferenceObject rtc_;
  int current_portions_count_{0};
  
  optional<int> get_initial_state_();
};

}  // namespace petfeeder
}  // namespace esphome
