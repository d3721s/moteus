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
#include "fw/millisecond_timer.h"
#include "fw/stm32_digital_output.h"
#include "fw/stm32_spi.h"

namespace moteus {

class KTH7111 {
 public:
  struct Options : public Stm32Spi::Options {
    uint8_t reg_cal = 0;

    Options(const Stm32Spi::Options& v) : Stm32Spi::Options(v) {}
  };

  struct SampleResult {
    uint32_t value = 0;
    bool valid = false;
  };

  struct ConfigStatus {
    uint8_t reg_cal = 0;
  };

  KTH7111(MillisecondTimer* timer, const Options& options)
      : timer_(timer),
        sda_(options.mosi, options.miso),
        cs_(options.cs, 1),
        sck_(options.sck, 1) {
    error_ = SetConfig(options);
  }

  uint32_t Sample() MOTEUS_CCM_ATTRIBUTE {
    StartSample();
    return FinishSample().value;
  }

  void StartSample() MOTEUS_CCM_ATTRIBUTE {
    cs_.clear();
    FrameDelay();
    sda_.output();
    TransferWrite(kReadAngle);
    sda_.input();
  }

  SampleResult FinishSample() MOTEUS_CCM_ATTRIBUTE {
    const uint8_t angle15_8 = TransferRead();
    const uint8_t angle7_0 = TransferRead();
    const uint8_t crc = TransferRead();
    FrameDelay();
    cs_.set();

    const uint32_t raw =
        (static_cast<uint32_t>(angle15_8) << 8) |
        static_cast<uint32_t>(angle7_0);
    return { raw, crc == CalculateCrc(angle15_8, angle7_0) };
  }

  bool error() const { return error_; }

  ConfigStatus config_status() const { return config_status_; }

 private:
  class BidirPin {
   public:
    BidirPin(PinName output_pin, PinName input_pin)
        : output_(GetGpio(output_pin), output_pin),
          input_(GetGpio(input_pin == NC ? output_pin : input_pin),
                 input_pin == NC ? output_pin : input_pin) {
      write(1);
      input();
      input_.set_input();
    }

    void output() MOTEUS_CCM_ATTRIBUTE {
      output_.set_output();
    }

    void input() MOTEUS_CCM_ATTRIBUTE {
      output_.set_input();
    }

    void write(int value) MOTEUS_CCM_ATTRIBUTE {
      output_.write(value);
    }

    bool read() const MOTEUS_CCM_ATTRIBUTE {
      return input_.read();
    }

   private:
    class Pin {
     public:
      Pin(GPIO_TypeDef* gpio, PinName pin)
          : moder_(&gpio->MODER),
            idr_(&gpio->IDR),
            bsrr_(&gpio->BSRR),
            brr_(&gpio->BRR),
            offset_(static_cast<uint32_t>(pin) & 0x0f),
            mask_(1 << offset_),
            mode_mask_(0x3 << (offset_ * 2)) {}

      void set_output() MOTEUS_CCM_ATTRIBUTE {
        *moder_ = (*moder_ & ~mode_mask_) | (1 << (offset_ * 2));
      }

      void set_input() MOTEUS_CCM_ATTRIBUTE {
        *moder_ = (*moder_ & ~mode_mask_);
      }

      void write(int value) MOTEUS_CCM_ATTRIBUTE {
        if (value) {
          *bsrr_ = mask_;
        } else {
          *brr_ = mask_;
        }
      }

      bool read() const MOTEUS_CCM_ATTRIBUTE {
        return (*idr_ & mask_) != 0;
      }

     private:
      volatile uint32_t* const moder_;
      volatile uint32_t* const idr_;
      volatile uint32_t* const bsrr_;
      volatile uint32_t* const brr_;
      const uint32_t offset_;
      const uint32_t mask_;
      const uint32_t mode_mask_;
    };

    static GPIO_TypeDef* GetGpio(PinName pin) {
      const uint32_t port_index = STM_PORT(pin);
      switch (port_index) {
        case PortA: return reinterpret_cast<GPIO_TypeDef*>(GPIOA_BASE);
        case PortB: return reinterpret_cast<GPIO_TypeDef*>(GPIOB_BASE);
        case PortC: return reinterpret_cast<GPIO_TypeDef*>(GPIOC_BASE);
        case PortD: return reinterpret_cast<GPIO_TypeDef*>(GPIOD_BASE);
        case PortE: return reinterpret_cast<GPIO_TypeDef*>(GPIOE_BASE);
        case PortF: return reinterpret_cast<GPIO_TypeDef*>(GPIOF_BASE);
      }
      MJ_ASSERT(false);
      return reinterpret_cast<GPIO_TypeDef*>(GPIOA_BASE);
    }

    Pin output_;
    Pin input_;
  };

  bool SetConfig(const Options& options) {
    bool result = false;

    uint8_t final_reg_cal = 0;
    result |= SetRegister(kRegCalReg,
                          options.reg_cal ? kRegCalBit : 0,
                          kRegCalBit,
                          &final_reg_cal,
                          false);
    config_status_.reg_cal = (final_reg_cal & kRegCalBit) ? 1 : 0;

    return result;
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
    cs_.clear();
    FrameDelay();
    sda_.output();
    TransferWrite(kReadRegister);
    TransferWrite(reg);
    sda_.input();
    const uint8_t value = TransferRead();
    const uint8_t crc = TransferRead();
    FrameDelay();
    cs_.set();

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
    cs_.clear();
    FrameDelay();
    sda_.output();
    TransferWrite(key >> 24);
    TransferWrite(key >> 16);
    TransferWrite(key >> 8);
    TransferWrite(key);
    sda_.input();
    FrameDelay();
    cs_.set();
  }

  void WriteRegister(uint8_t reg, uint8_t value) {
    cs_.clear();
    FrameDelay();
    sda_.output();
    TransferWrite(kWriteRegister);
    TransferWrite(reg);
    TransferWrite(value);
    sda_.input();
    FrameDelay();
    cs_.set();
  }

  void TransferWrite(uint8_t value) MOTEUS_CCM_ATTRIBUTE {
    for (int i = 7; i >= 0; i--) {
      sda_.write((value & (1 << i)) ? 1 : 0);
      sck_.clear();
      Delay();
      sck_.set();
      Delay();
    }
  }

  uint8_t TransferRead() MOTEUS_CCM_ATTRIBUTE {
    uint8_t result = 0;
    for (int i = 7; i >= 0; i--) {
      sck_.clear();
      Delay();
      sck_.set();
      Delay();
      result = (result << 1) | (sda_.read() ? 1 : 0);
    }
    return result;
  }

  static void Delay() MOTEUS_CCM_ATTRIBUTE {
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
    __asm__ volatile("nop");
  }

  static void FrameDelay() MOTEUS_CCM_ATTRIBUTE {
    Delay();
    Delay();
    Delay();
    Delay();
  }

  static uint8_t CalculateCrc(uint8_t value) MOTEUS_CCM_ATTRIBUTE {
    return UpdateCrc(0x00, value) ^ 0x55;
  }

  static uint8_t CalculateCrc(uint8_t high, uint8_t low) MOTEUS_CCM_ATTRIBUTE {
    uint8_t crc = 0x00;
    crc = UpdateCrc(crc, high);
    crc = UpdateCrc(crc, low);
    return crc ^ 0x55;
  }

  static uint8_t UpdateCrc(uint8_t crc, uint8_t value) MOTEUS_CCM_ATTRIBUTE {
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
  static constexpr uint8_t kRegCalReg = 0x16;
  static constexpr uint8_t kRegCalBit = 0x10;

  MillisecondTimer* const timer_;
  BidirPin sda_;
  Stm32DigitalOutput cs_;
  Stm32DigitalOutput sck_;
  ConfigStatus config_status_;
  bool error_ = false;
};

}
