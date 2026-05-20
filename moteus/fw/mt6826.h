// Copyright 2026 mjbots Robotic Systems, LLC.  info@mjbots.com
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "mbed.h"

#include "fw/ccm.h"
#include "fw/stm32_spi.h"

namespace moteus {

class MT6826 {
 public:
  using Options = Stm32Spi::Options;

  struct SampleResult {
    uint32_t value = 0;
    uint8_t status = 0;
    bool valid = false;
  };

  MT6826(const Options& options)
      : spi_([&]() {
               auto copy = options;
               copy.mode = 3;
               copy.width = 8;
               return copy;
             }()) {
  }

  void StartSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    tx_[0] = kBurstReadAngle0;
    tx_[1] = kBurstReadAngle1;
    tx_[2] = 0x00;
    tx_[3] = 0x00;
    tx_[4] = 0x00;
    tx_[5] = 0x00;
    StartDma(kFrameSize);
  }

  SampleResult FinishSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    spi_.finish_dma_transfer();

    const uint32_t raw =
        (static_cast<uint32_t>(rx_[2]) << 7) |
        (static_cast<uint32_t>(rx_[3]) >> 1);
    const uint8_t status = rx_[4] & 0x07;
    const uint8_t crc = CalculateCrc(rx_[2], rx_[3], rx_[4]);

    return { raw, status, crc == rx_[5] };
  }

 private:
  void StartDma(int size) MOTEUS_CCM_ATTRIBUTE {
    spi_.start_dma_transfer(
        std::string_view(reinterpret_cast<const char*>(&tx_[0]), size),
        mjlib::base::string_span(reinterpret_cast<char*>(&rx_[0]), size));
  }

  static uint8_t CalculateCrc(uint8_t angle14_7,
                              uint8_t angle6_0,
                              uint8_t status)
      MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    uint8_t crc = 0x00;
    crc = UpdateCrc(crc, angle14_7);
    crc = UpdateCrc(crc, angle6_0);
    crc = UpdateCrc(crc, status);
    return crc;
  }

  static uint8_t UpdateCrc(uint8_t crc, uint8_t value)
      MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    crc ^= value;
    for (int i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
  }

  static constexpr uint8_t kBurstReadAngle0 = 0xa0;
  static constexpr uint8_t kBurstReadAngle1 = 0x03;
  static constexpr int kFrameSize = 6;

  Stm32Spi spi_;
  uint8_t tx_[kFrameSize] = {};
  uint8_t rx_[6] = {};
};

}
