import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import api, sensor, uart, time
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_EMPTY,
)

CONF_PORTIONS_COUNTER = "portions_counter"
CONF_TIME_ID = "time_id"

DEPENDENCIES = ["uart", "api"]
AUTO_LOAD = ["time"]
CODEOWNERS = ["jeromelaban"]
MULTI_CONF = True

# Component namespace
petfeeder_ns = cg.esphome_ns.namespace("petfeeder")
PetFeederComponent = petfeeder_ns.class_(
    "PetFeederComponent", cg.Component, uart.UARTDevice
)
PetFeederPortionsCounterComponent = petfeeder_ns.class_(
    "PetFeederPortionsCounterComponent", cg.Component, sensor.Sensor
)

# Configuration schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PetFeederComponent),
        cv.Optional("portions_counter"): sensor.sensor_schema(
            PetFeederPortionsCounterComponent,
            unit_of_measurement=UNIT_EMPTY,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
).extend(uart.UART_DEVICE_SCHEMA)


# This schema is used for validation in Home Assistant/YAML only
# The actual struct is defined in C++ code
FEEDING_SCHEDULE_SCHEMA = cv.Schema({
    cv.Required("hour"): cv.int_range(min=0, max=23),
    cv.Required("minute"): cv.int_range(min=0, max=59),
    cv.Required("portions"): cv.int_range(min=1, max=255),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_PORTIONS_COUNTER in config:
        sens = await sensor.new_sensor(config[CONF_PORTIONS_COUNTER])
        cg.add(var.set_counter_component(sens))

    # Handle time component if specified
    if CONF_TIME_ID in config:
        time_ = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_))
    
    # The service registration is handled in the C++ code
    # Services are registered in the setup() method of PetFeederComponent
