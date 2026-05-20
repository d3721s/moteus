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
#include "fw/millisecond_timer.h"
#include "fw/stm32_spi.h"

namespace moteus {

class KTH7812 {
 public:
  static constexpr uint8_t kTrimNone = 0x00;
  static constexpr uint8_t kTrimX = 0x01;
  static constexpr uint8_t kTrimY = 0x02;
  static constexpr uint8_t kTrimMask = kTrimX | kTrimY;

  struct Options : public Stm32Spi::Options {
    PinName mgh = NC;
    PinName mgl = NC;
    uint8_t gt = 2;
    uint8_t trim = kTrimNone;

    Options(const Stm32Spi::Options& v) : Stm32Spi::Options(v) {}
  };

  struct ConfigStatus {
    uint8_t gt = 0;
    uint8_t trim = kTrimNone;
  };

  KTH7812(MillisecondTimer* timer, const Options& options)
      : timer_(timer),
        spi_([&]() {
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

    error_ = SetConfig(options);
  }

  uint16_t Sample() {
    return spi_.write(kReadAngle);
  }

  void StartSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    return spi_.start_write(kReadAngle);
  }

  uint16_t FinishSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    return spi_.finish_write();
  }

  bool magnetic_field_high() MOTEUS_CCM_ATTRIBUTE {
    return mgh_ && mgh_->read();
  }

  bool magnetic_field_low() MOTEUS_CCM_ATTRIBUTE {
    return mgl_ && mgl_->read();
  }

  bool error() const { return error_; }

  ConfigStatus config_status() const { return config_status_; }

 private:
  bool SetConfig(const Options& options) {
    EnableRegisterWrites();

    bool result = false;

    result |= SetRegister(kGainTrimReg, options.gt, 0xff,
                          &config_status_.gt);

    uint8_t trim_status = kTrimNone;
    result |= SetRegister(kTrimReg, options.trim, kTrimMask, &trim_status);
    config_status_.trim = trim_status & kTrimMask;

    return result;
  }

  bool SetRegister(
      uint8_t reg, uint8_t desired, uint8_t mask, uint8_t* final_value) {
    const auto current_value = ReadRegister(reg);
    if ((current_value & mask) == (desired & mask)) {
      *final_value = current_value;
      return false;
    }

    const uint8_t write_value =
        (current_value & ~mask) | (desired & mask);
    *final_value = BurnRegister(reg, write_value);
    return (*final_value & mask) != (desired & mask);
  }

  uint8_t ReadRegister(uint8_t reg) {
    spi_.write(kReadRegister | (reg << 8));
    timer_->wait_us(2);
    return spi_.write(kReadAngle) >> 8;
  }

  void EnableRegisterWrites() {
    spi_.write(kRegisterWriteEnable);
    timer_->wait_us(2);
  }

  uint8_t BurnRegister(uint8_t reg, uint8_t value) {
    // KTH7812 register writes are MTP burns.  The second frame, after the
    // required wait, returns the newly written register value.
    spi_.write(kWriteRegister | (reg << 8) | value);
    timer_->wait_ms(kMtpBurnMs);
    return spi_.write(kReadAngle) >> 8;
  }

  static constexpr uint16_t kReadAngle = 0x0000;
  static constexpr uint16_t kReadRegister = 0x4000;
  static constexpr uint16_t kWriteRegister = 0x8000;
  static constexpr uint16_t kRegisterWriteEnable = 0xe800;
  static constexpr uint8_t kGainTrimReg = 0x02;
  static constexpr uint8_t kTrimReg = 0x03;
  static constexpr uint32_t kMtpBurnMs = 20;

  MillisecondTimer* const timer_;
  Stm32Spi spi_;
  std::optional<DigitalIn> mgh_;
  std::optional<DigitalIn> mgl_;
  ConfigStatus config_status_;
  bool error_ = false;
};

}
