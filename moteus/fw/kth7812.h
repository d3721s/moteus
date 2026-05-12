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

#include <optional>

#include "mbed.h"

#include "fw/ccm.h"
#include "fw/stm32_spi.h"

namespace moteus {

class KTH7812 {
 public:
  struct Options : public Stm32Spi::Options {
    PinName mgh = NC;
    PinName mgl = NC;

    Options(const Stm32Spi::Options& v) : Stm32Spi::Options(v) {}
  };

  KTH7812(const Options& options)
      : spi_([&]() {
          auto copy = options;
          copy.mode = 3;
          copy.width = 16;
          return copy;
        }()) {
    if (options.mgh != NC) {
      mgh_.emplace(options.mgh);
    }
    if (options.mgl != NC) {
      mgl_.emplace(options.mgl);
    }
  }

  uint16_t Sample() MOTEUS_CCM_ATTRIBUTE {
    return spi_.write(kReadAngle);
  }

  void StartSample() MOTEUS_CCM_ATTRIBUTE {
    return spi_.start_write(kReadAngle);
  }

  uint16_t FinishSample() MOTEUS_CCM_ATTRIBUTE {
    return spi_.finish_write();
  }

  bool magnetic_field_high() MOTEUS_CCM_ATTRIBUTE {
    return mgh_ && mgh_->read();
  }

  bool magnetic_field_low() MOTEUS_CCM_ATTRIBUTE {
    return mgl_ && mgl_->read();
  }

 private:
  static constexpr uint16_t kReadAngle = 0x0000;

  Stm32Spi spi_;
  std::optional<DigitalIn> mgh_;
  std::optional<DigitalIn> mgl_;
};

}
