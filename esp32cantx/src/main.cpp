// Bench CAN transmitter: valid 1 Mbit/s standard frames in TWAI no-ack mode.
// No second node is required, and the RP2040 under test stays receive-only.

#include <Arduino.h>
#include <Wire.h>
#include <driver/twai.h>
#include <freertos/FreeRTOS.h>

namespace {
constexpr gpio_num_t kTx = GPIO_NUM_20;
constexpr gpio_num_t kRx = GPIO_NUM_19;
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;
constexpr uint8_t kCh422gWrSet = 0x24;
constexpr uint8_t kCh422gWrOc = 0x23;
constexpr uint8_t kCh422gWrIo = 0x38;
// An 8-byte standard frame is 111..121 bits including stuffing and IFS.
// The mean is ~114 bits, so 126 us at 1 Mbit/s is ~90% average bus load.
constexpr uint32_t kFramePeriodUs = 126;
uint32_t s_counter = 0;
uint32_t s_tx_ok = 0;
uint32_t s_tx_fail = 0;

bool ch422g_write(uint8_t addr, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// The Waveshare board only connects GPIO19/20 to the TJA1051 while the
// CH422G's USB_SEL output (IO5) is high.  The expander powers up low, so a
// board power cycle disconnects CAN until this runs.
bool enable_can_mux() {
    Wire.begin(kI2cSda, kI2cScl, 400000);
    if (!ch422g_write(kCh422gWrSet, 0x01)) return false;
    if (!ch422g_write(kCh422gWrOc, 0x0F)) return false;
    if (!ch422g_write(kCh422gWrIo, 0xFF)) return false;
    delay(5);
    return true;
}
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    if (!enable_can_mux())
        Serial.println("esp32 cantx: CH422G USB_SEL/CAN mux enable failed");

    twai_general_config_t g =
        TWAI_GENERAL_CONFIG_DEFAULT(kTx, kRx, TWAI_MODE_NO_ACK);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    const esp_err_t ir = twai_driver_install(&g, &t, &f);
    if (ir != ESP_OK) {
        Serial.printf("twai_driver_install failed: %d\n", (int)ir);
        return;
    }
    const esp_err_t sr = twai_start();
    if (sr != ESP_OK) {
        Serial.printf("twai_start failed: %d\n", (int)sr);
        return;
    }
    Serial.printf("esp32 cantx: 1 Mbit/s, no-ack, id=0x123, ~90%% load (%lu us)\n",
                  (unsigned long)kFramePeriodUs);
}

void loop() {
    static uint32_t next_us;
    if (next_us == 0)
        next_us = micros();
    const int32_t wait = (int32_t)(next_us - micros());
    if (wait > 0)
        delayMicroseconds((uint32_t)wait);
    next_us += kFramePeriodUs;
    if ((int32_t)(micros() - next_us) > (int32_t)kFramePeriodUs)
        next_us = micros();

    twai_message_t m = {};
    m.identifier = 0x123;
    m.data_length_code = 8;
    m.flags = 0;
    for (int i = 0; i < 8; i++) {
        m.data[i] = (uint8_t)(s_counter + (uint32_t)i);
    }
    s_counter++;

    const esp_err_t r = twai_transmit(&m, pdMS_TO_TICKS(10));
    if (r == ESP_OK) {
        s_tx_ok++;
    } else {
        s_tx_fail++;
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK &&
            st.state == TWAI_STATE_BUS_OFF) {
            twai_initiate_recovery();
        }
    }

    static uint32_t last_ms;
    if ((uint32_t)(millis() - last_ms) >= 1000u) {
        last_ms = millis();
        twai_status_info_t st;
        twai_get_status_info(&st);
        Serial.printf("tx ok=%lu fail=%lu state=%d tec=%lu rec=%lu\n",
                      (unsigned long)s_tx_ok, (unsigned long)s_tx_fail,
                      (int)st.state, (unsigned long)st.tx_error_counter,
                      (unsigned long)st.rx_error_counter);
    }
}
