/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 *
 * fakeTec / ProMicro nRF52840 DIY, from MeshCore's variants/promicro/variant.h. MeshMesh uses nRF52840 GPIO
 * numbers as Arduino pin numbers (identity map in variant.cpp, as the Heltec T096 variant), so the pins of the
 * promicro map are written here as GPIO numbers (P1.xx = 32+xx).
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define VARIANT_MCK       (64000000ul)

#define USE_LFRC    // ProMicro modules have no 32.768 kHz crystal

////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_EXT_VCC          (13)   // P0.13: the SX1262 module supply on the fakeTec PCB
#define EXT_VCC              (PIN_EXT_VCC)

#define BATTERY_PIN          (31)   // P0.31 (AIN7)
#define ADC_RESOLUTION       12

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (3)
#define NUM_ANALOG_OUTPUTS   (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition (the GPS)

#define PIN_SERIAL1_RX       (20)   // P0.20
#define PIN_SERIAL1_TX       (22)   // P0.22

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (36)   // P1.04
#define PIN_WIRE_SCL         (11)   // P0.11

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition (the SX1262)

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_SCK          (43)   // P1.11
#define PIN_SPI_MISO         (2)    // P0.02
#define PIN_SPI_MOSI         (47)   // P1.15
#define PIN_SPI_NSS          (45)   // P1.13

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED              (15)   // P0.15
#define LED_PIN              PIN_LED
#define LED_BLUE             PIN_LED
#define LED_BUILTIN          PIN_LED
#define LED_STATE_ON         1

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1          (32)   // P1.00
#define BUTTON_PIN           PIN_BUTTON1
