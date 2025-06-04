# Pet Feeder External Component for ESPHome

This is an ESPHome external component for controlling an ESP8266-based Wifi pet feeder device via UART to an internal MCU.

> [!IMPORTANT]
> Using this component does require physical modifications to your device that will void your warranty. Those modifications are not detailed in this repo.

![image of the pet feeder](petfeeder/pet-feeder.png)

## Features

- Feed pet with specified portions
- Track the total portions dispensed
- Automatically indicate network state via device LED
- Schedule automatic feedings at specific times of day (works even offline)
- Stores feeding schedules in flash memory
- Triggers Home Assistant events when an automated feeding occurs

## Installation

In your ESPHome configuration, add:

```yaml
external_components:
  - source: github://jeromelaban/esphome-petfeeder
    components: [petfeeder]
```

## Configuration

```yaml
# Required components
api:

uart:
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 9600

# Time component is required for scheduled feedings
time:
  - platform: homeassistant
    id: homeassistant_time

petfeeder:
  portions_counter:
    name: "Pet Feeder Portions Counter"
  time_id: homeassistant_time  # Optional: required only if using scheduled feedings
```

### Offline Operation

For offline operation, you may want to use a different time source such as NTP or a real-time clock (RTC) module:

```yaml
# Example configuration for offline operation with NTP time
time:
  - platform: sntp
    id: ntp_time

petfeeder:
  portions_counter:
    name: "Pet Feeder Portions Counter"
  time_id: ntp_time
```

## How Offline Operation Works

The pet feeder component has been enhanced to work reliably even without a network connection:

1. **Persistent Storage**: All feeding schedules are stored in the ESP8266's flash memory using ESPHome's preferences system.
2. **Time-Based Triggering**: The component checks the current time once per minute and triggers feedings if it matches any of the configured schedules.
3. **Network Status Indication**: The device's LED indicates the network connection status automatically:
   - Steady light: Connected to network
   - Slow blinking: Offline but still operational
4. **Automatic Recovery**: When the network connection is restored, the component will automatically reconnect and continue reporting to Home Assistant.

## Services

### `petfeeder.feed_pet`
Feed pet with specified portions.

Parameters:
- `portions`: Number of portions to dispense (integer)

Example service call:
```yaml
service:
  - service: petfeeder.feed_pet
    data:
      portions: 2
```

### `petfeeder.test_message`
Send a test message to the device (for debugging).

Parameters:
- `target`: Target address (integer)
- `source`: Source address (integer)
- `command`: Command value (integer)
- `value`: Value to send (integer)

### `petfeeder.add_feeding_schedule`
Add an automatic feeding schedule. You can call this method multiple times to create multiple feeding times throughout the day. Schedules are saved in flash memory and will work even when the device is offline.

Parameters:
- `hour`: Hour of the day (0-23)
- `minute`: Minute of the hour (0-59)
- `portions`: Number of portions to dispense (integer)

Example service call:
```yaml
service:
  - service: petfeeder.add_feeding_schedule
    data:
      hour: 8
      minute: 0
      portions: 2
  
  - service: petfeeder.add_feeding_schedule
    data:
      hour: 18
      minute: 30
      portions: 2
```

### `petfeeder.clear_feeding_schedules`
Remove all configured feeding schedules.

Example service call:
```yaml
service:
  - service: petfeeder.clear_feeding_schedules
```

## Home Assistant Events

The pet feeder component triggers events in Home Assistant that you can use for automations:

### `esphome.petfeeder_auto_feeding`

Triggered when an automatic feeding occurs based on a schedule.

Event data:

- `hour`: Hour when the feeding occurred (0-23)
- `minute`: Minute when the feeding occurred (0-59)
- `portions`: Number of portions dispensed

### Example Automation

```yaml
automation:
  - alias: "Pet Feeder Notification"
    trigger:
      platform: event
      event_type: esphome.petfeeder_auto_feeding
    action:
      - service: notify.mobile_app
        data:
          title: "Pet Feeder Activated"
          message: "Pet was automatically fed {{ trigger.event.data.portions }} portions at {{ trigger.event.data.hour }}:{{ trigger.event.data.minute }}"
```
