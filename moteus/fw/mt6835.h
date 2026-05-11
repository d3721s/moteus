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
#include "fw/stm32_digital_monitor.h"
#include "fw/stm32_digital_output.h"
#include "fw/stm32_spi.h"

namespace moteus {

class MT6835 {
 public:
  using Options = Stm32Spi::Options;

  MT6835(const Options& options)
      : miso_input_(options.miso),
        cs_(options.cs, 1),
        mosi_(options.mosi, 0),
        miso_(options.miso),
        sck_(options.sck, 1) {
  }

  uint32_t Sample() MOTEUS_CCM_ATTRIBUTE {
    StartSample();
    return FinishSample();
  }

  void StartSample() MOTEUS_CCM_ATTRIBUTE {
    cs_.clear();
    Delay();
    rx_[0] = Transfer(kBurstReadAngle0);
  }

  uint32_t FinishSample() MOTEUS_CCM_ATTRIBUTE {
    rx_[1] = Transfer(kBurstReadAngle1);
    rx_[2] = Transfer(0x00);
    rx_[3] = Transfer(0x00);
    rx_[4] = Transfer(0x00);
    cs_.set();

    const uint32_t raw =
        (static_cast<uint32_t>(rx_[2]) << 13) |
        (static_cast<uint32_t>(rx_[3]) << 5) |
        (static_cast<uint32_t>(rx_[4]) >> 3);

    return raw;
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

  static constexpr uint8_t kBurstReadAngle0 = 0xa0;
  static constexpr uint8_t kBurstReadAngle1 = 0x03;

  DigitalIn miso_input_;
  Stm32DigitalOutput cs_;
  Stm32DigitalOutput mosi_;
  Stm32DigitalMonitor miso_;
  Stm32DigitalOutput sck_;
  uint8_t rx_[5] = {};
};

}
