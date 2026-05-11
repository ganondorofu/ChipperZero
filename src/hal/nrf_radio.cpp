#include "nrf_radio.h"

#include <SPI.h>
#include <freertos/semphr.h>

#include "pins.h"

extern SemaphoreHandle_t spi_mutex;

// 4 MHz chosen for noise tolerance on longer wires with PA+LNA module.
RF24 g_nrf_radio(PIN_NRF_CE, PIN_NRF_CSN, 4000000);

bool nrfLockSpi(TickType_t timeout) {
    return spi_mutex && xSemaphoreTake(spi_mutex, timeout) == pdTRUE;
}

void nrfUnlockSpi() {
    if (spi_mutex) xSemaphoreGive(spi_mutex);
}
