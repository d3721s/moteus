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

class KTH7111 {
 public:
  struct Options : public Stm32Spi::Options {
    uint8_t reg_cal = 0;
    uint8_t anlc_en = 0;
    uint8_t gaintrim = 0xac;

    Options(const Stm32Spi::Options& v) : Stm32Spi::Options(v) {}
  };

  struct SampleResult {
    uint32_t value = 0;
    bool valid = false;
  };

  struct ConfigStatus {
    uint8_t reg_cal = 0;
    uint8_t anlc_en = 0;
    uint8_t gaintrim = 0;
    uint8_t anlc_status = 0;
  };

  KTH7111(const Options& options)
      : spi_([&]() {
               auto copy = options;
               copy.mode = 3;
               copy.width = 8;
               return copy;
             }()) {
    error_ = SetConfig(options);
  }

  uint32_t Sample() {
    StartSample();
    return FinishSample().value;
  }

  void StartSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    tx_[0] = kReadAngle;
    tx_[1] = 0x00;
    tx_[2] = 0x00;
    tx_[3] = 0x00;
    StartDma(kAngleFrameSize);
  }

  SampleResult FinishSample() MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    spi_.finish_dma_transfer();
    const uint8_t angle15_8 = rx_[1];
    const uint8_t angle7_0 = rx_[2];
    const uint8_t crc = rx_[3];

    const uint32_t raw =
        (static_cast<uint32_t>(angle15_8) << 8) |
        static_cast<uint32_t>(angle7_0);
    return { raw, crc == CalculateCrc(angle15_8, angle7_0) };
  }

  bool error() const { return error_; }

  ConfigStatus config_status() const { return config_status_; }

 private:
  bool SetConfig(const Options& options) {
    bool result = false;

    uint8_t final_gaintrim = 0;
    result |= SetRegister(kGainTrimReg,
                          options.gaintrim,
                          0xff,
                          &final_gaintrim,
                          true);
    config_status_.gaintrim = final_gaintrim;

    uint8_t final_anlc_config = 0;
    result |= SetRegister(kRegCalReg,
                          (options.reg_cal ? kRegCalBit : 0) |
                              (options.anlc_en ? kAnlcEnableBit : 0),
                          kRegCalBit | kAnlcEnableBit,
                          &final_anlc_config,
                          false);
    config_status_.reg_cal =
        (final_anlc_config & kRegCalBit) ? 1 : 0;
    config_status_.anlc_en =
        (final_anlc_config & kAnlcEnableBit) ? 1 : 0;

    result |= !UpdateAnlcStatus();

    return result;
  }

  bool UpdateAnlcStatus() {
    bool read_ok = false;
    const auto anlc_status = ReadRegister(kAnlcStatusReg, &read_ok);
    if (!read_ok) { return false; }

    config_status_.anlc_status =
        (anlc_status & kAnlcStatusMask) >> kAnlcStatusShift;
    return true;
  }

  bool SetRegister(uint8_t reg,
                   uint8_t desired,
                   uint8_t mask,
                   uint8_t* final_value,
                   bool verify) {
    bool read_ok = false;
    const auto current_value = ReadRegister(reg, &read_ok);
    if (!read_ok) { return true; }

    if ((current_value & mask) == (desired & mask)) {
      *final_value = current_value;
      return false;
    }

    const uint8_t write_value =
        (current_value & ~mask) | (desired & mask);

    UnlockRegisters();
    WriteRegister(reg, write_value);
    LockRegisters();

    const auto new_value = ReadRegister(reg, &read_ok);
    if (!read_ok) { return true; }

    *final_value = new_value;
    return verify && ((*final_value & mask) != (desired & mask));
  }

  uint8_t ReadRegister(uint8_t reg, bool* ok) {
    tx_[0] = kReadRegister;
    tx_[1] = reg;
    tx_[2] = 0x00;
    tx_[3] = 0x00;
    DmaTransfer(kRegisterReadFrameSize);
    const uint8_t value = rx_[2];
    const uint8_t crc = rx_[3];

    *ok = (crc == CalculateCrc(value));
    return value;
  }

  void UnlockRegisters() {
    WriteKey(kUnlockKey);
  }

  void LockRegisters() {
    WriteKey(kLockKey);
  }

  void WriteKey(uint32_t key) {
    tx_[0] = key >> 24;
    tx_[1] = key >> 16;
    tx_[2] = key >> 8;
    tx_[3] = key;
    DmaTransfer(kKeyFrameSize);
  }

  void WriteRegister(uint8_t reg, uint8_t value) {
    tx_[0] = kWriteRegister;
    tx_[1] = reg;
    tx_[2] = value;
    DmaTransfer(kRegisterWriteFrameSize);
  }

  void DmaTransfer(int size) {
    StartDma(size);
    spi_.finish_dma_transfer();
  }

  void StartDma(int size) MOTEUS_CCM_ATTRIBUTE {
    spi_.start_dma_transfer(
        std::string_view(reinterpret_cast<const char*>(&tx_[0]), size),
        mjlib::base::string_span(reinterpret_cast<char*>(&rx_[0]), size));
  }

  static uint8_t CalculateCrc(uint8_t value) MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    return UpdateCrc(0x00, value) ^ 0x55;
  }

  static uint8_t CalculateCrc(uint8_t high, uint8_t low)
      MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    uint8_t crc = 0x00;
    crc = UpdateCrc(crc, high);
    crc = UpdateCrc(crc, low);
    return crc ^ 0x55;
  }

  static uint8_t UpdateCrc(uint8_t crc, uint8_t value)
      MOTEUS_CCM_NOINLINE_ATTRIBUTE {
    crc ^= value;
    for (int i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
  }

  static constexpr uint8_t kReadAngle = 0x00;
  static constexpr uint8_t kReadRegister = 0x11;
  static constexpr uint8_t kWriteRegister = 0x33;
  static constexpr uint32_t kUnlockKey = 0x20240101;
  static constexpr uint32_t kLockKey = 0x20241231;
  static constexpr uint8_t kGainTrimReg = 0x11;
  static constexpr uint8_t kRegCalReg = 0x16;
  static constexpr uint8_t kAnlcStatusReg = 0x72;
  static constexpr uint8_t kRegCalBit = 0x10;
  static constexpr uint8_t kAnlcEnableBit = 0x08;
  static constexpr uint8_t kAnlcStatusMask = 0x30;
  static constexpr uint8_t kAnlcStatusShift = 4;
  static constexpr int kAngleFrameSize = 4;
  static constexpr int kRegisterReadFrameSize = 4;
  static constexpr int kRegisterWriteFrameSize = 3;
  static constexpr int kKeyFrameSize = 4;
  static constexpr int kMaxFrameSize = 4;

  Stm32Spi spi_;
  uint8_t tx_[kMaxFrameSize] = {};
  uint8_t rx_[kMaxFrameSize] = {};
  ConfigStatus config_status_;
  bool error_ = false;
};

}
