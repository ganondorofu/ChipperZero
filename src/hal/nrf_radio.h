#pragma once

#include <RF24.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Single RF24 instance shared by nrf_spam and nrf_ble_spam.
// All access must be protected by spi_mutex via nrfLockSpi/nrfUnlockSpi.
extern RF24 g_nrf_radio;

bool nrfLockSpi(TickType_t timeout = pdMS_TO_TICKS(100));
void nrfUnlockSpi();
