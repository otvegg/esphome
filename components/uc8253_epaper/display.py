"""Custom display driver for the WeAct Studio 3.7in black/white e-paper panel
(240x416 pixels, UC8253 controller). Not natively supported by ESPHome.

Register sequence ported from the GDEY037T03 driver in the GxEPD2 Arduino
library (https://github.com/ZinggJM/GxEPD2). See uc8253_epaper.cpp/.h for
details.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import display, spi
from esphome.const import CONF_BUSY_PIN, CONF_DC_PIN, CONF_ID, CONF_LAMBDA, CONF_RESET_PIN

DEPENDENCIES = ["spi"]

uc8253_epaper_ns = cg.esphome_ns.namespace("uc8253_epaper")
UC8253EPaper = uc8253_epaper_ns.class_(
    "UC8253EPaper", cg.PollingComponent, spi.SPIDevice, display.DisplayBuffer
)

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(UC8253EPaper),
        cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
    }
).extend(spi.spi_device_schema())

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "uc8253_epaper", require_miso=False, require_mosi=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await display.register_display(var, config)
    await spi.register_spi_device(var, config, write_only=True)

    dc = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))

    if CONF_RESET_PIN in config:
        reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset))

    if CONF_BUSY_PIN in config:
        busy = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
        cg.add(var.set_busy_pin(busy))
