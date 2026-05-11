#include "storage.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../hal/pins.h"

StorageModule g_storage;

extern SemaphoreHandle_t spi_mutex;

// ---- File list --------------------------------------------------------------

static char  s_files[8][32];
static uint8_t s_fileCount = 0;

static void listRoot() {
    s_fileCount = 0;
    File root = SD.open("/");
    if (!root) return;
    while (s_fileCount < 8) {
        File f = root.openNextFile();
        if (!f) break;
        strncpy(s_files[s_fileCount], f.name(), 31);
        s_files[s_fileCount][31] = '\0';
        s_fileCount++;
        f.close();
    }
    root.close();
}

// ---- helpers ----------------------------------------------------------------

void StorageModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

// ---- Task -------------------------------------------------------------------

static void storageTask(void* arg) {
    StorageModule* self = reinterpret_cast<StorageModule*>(arg);
    self->setStatus("Mounting SD...");

    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        self->setStatus("SPI busy");
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }
    bool ok = SD.begin(PIN_SD_CS);
    xSemaphoreGive(spi_mutex);

    if (!ok) {
        self->setStatus("No SD card");
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Get card info
    uint64_t total = SD.totalBytes() / (1024 * 1024);
    uint64_t used  = SD.usedBytes()  / (1024 * 1024);
    char info[48];
    snprintf(info, sizeof(info), "%lluMB / %lluMB\nListing...", used, total);
    self->setStatus(info);

    listRoot();

    // Build status: first visible file
    char buf[48];
    if (s_fileCount == 0) {
        snprintf(buf, sizeof(buf), "%lluMB / %lluMB\n(empty)", used, total);
    } else {
        uint8_t idx = self->scroll() < s_fileCount ? self->scroll() : 0;
        snprintf(buf, sizeof(buf), "%llu/%lluMB  %u files\n%s",
                 used, total, s_fileCount, s_files[idx]);
    }
    self->setStatus(buf);

    // Idle loop: refresh display on scroll events
    while (self->isRunning()) {
        if (s_fileCount > 0) {
            uint8_t idx = self->scroll() < s_fileCount ? self->scroll() : s_fileCount - 1;
            snprintf(buf, sizeof(buf), "%llu/%lluMB  %u files\n%s",
                     used, total, s_fileCount, s_files[idx]);
            self->setStatus(buf);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    xSemaphoreTake(spi_mutex, portMAX_DELAY);
    SD.end();
    xSemaphoreGive(spi_mutex);

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void StorageModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    scroll_ = 0;
    s_fileCount = 0;
    xTaskCreatePinnedToCore(storageTask, "storage", 4096, this, 1, &task_, 0);
}

void StorageModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void StorageModule::onEvent(uint8_t ev) {
    if (ev == 2) scrollDown();
    else if (ev == 1) scrollUp();
}

void StorageModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
