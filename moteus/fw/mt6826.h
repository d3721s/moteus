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
#include "fw/stm32_digital_output.h"
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
      : miso_(options.miso),
        cs_(options.cs, 1),
        mosi_(options.mosi, 0),
        sck_(options.sck, 1) {
  }

  uint32_t Sample() MOTEUS_CCM_ATTRIBUTE {
    StartSample();
    return FinishSample().value;
  }

  void StartSample() MOTEUS_CCM_ATTRIBUTE {
    cs_.clear();
    Delay();
    rx_[0] = Transfer(kBurstReadAngle0);
  }

  SampleResult FinishSample() MOTEUS_CCM_ATTRIBUTE {
    rx_[1] = Transfer(kBurstReadAngle1);
    rx_[2] = Transfer(0x00);
    rx_[3] = Transfer(0x00);
    rx_[4] = Transfer(0x00);
    rx_[5] = Transfer(0x00);
    Delay();
    cs_.set();

    const uint32_t raw =
        (static_cast<uint32_t>(rx_[2]) << 7) |
        (static_cast<uint32_t>(rx_[3]) >> 1);
    const uint8_t status = rx_[4] & 0x07;
    const uint8_t crc = CalculateCrc(rx_[2], rx_[3], rx_[4]);

    return { raw, status, crc == rx_[5] };
  }

 private:
  uint8_t Transfer(uint8_t value) MOTEUS_CCM_ATTRIBUTE {
    uint8_t result = 0;
    for (int i = 7; i >= 0; i--) {
      mosi_.write((value & (1 << i)) ? 1 : 0);
      sck_.clear();
      Delay();
      sck_.set();
      Delay();
      result = (result << 1) | (miso_.read() ? 1 : 0);
    }
    return result;
  }

  static void Delay() MOTEUS_CCM_ATTRIBUTE {
    __asm__ volatile("nop");
    __asm__ volatile("nop");
  }

  static uint8_t CalculateCrc(uint8_t angle14_7,
                              uint8_t angle6_0,
                              uint8_t status) MOTEUS_CCM_ATTRIBUTE {
    uint8_t crc = 0x00;
    crc = UpdateCrc(crc, angle14_7);
    crc = UpdateCrc(crc, angle6_0);
    crc = UpdateCrc(crc, status);
    return crc;
  }

  static uint8_t UpdateCrc(uint8_t crc, uint8_t value) MOTEUS_CCM_ATTRIBUTE {
    crc ^= value;
    for (int i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
  }

  static constexpr uint8_t kBurstReadAngle0 = 0xa0;
  static constexpr uint8_t kBurstReadAngle1 = 0x03;

  DigitalIn miso_;
  Stm32DigitalOutput cs_;
  Stm32DigitalOutput mosi_;
  Stm32DigitalOutput sck_;
  uint8_t rx_[6] = {};
};

}
