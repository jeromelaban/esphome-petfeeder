import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import api, sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_EMPTY,
)

CONF_PORTIONS_COUNTER = "portions_counter"

DEPENDENCIES = ["uart", "api"]
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
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_PORTIONS_COUNTER in config:
        sens = await sensor.new_sensor(config[CONF_PORTIONS_COUNTER])
        cg.add(var.set_counter_component(sens))
