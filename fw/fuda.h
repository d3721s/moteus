#pragma once

#include <cstdint>

#include "mjlib/base/visitor.h"
#include "mjlib/micro/persistent_config.h"

namespace moteus {

struct Fuda {
  Fuda();

  struct Status {
    float velocity = 0.0f;
    float position = 0.0f;
    int32_t hall_offset = 0;
    int32_t hall_value = 0;
    uint32_t status_code = 0;
    uint32_t errors_code = 0;
    float board_ntc = 0.0f;
    float iq_current = 0.0f;

    bool switched_on = false;
    bool target_reached = false;
    bool current_limit_active = false;

    bool adc_selftest_fatal = false;
    bool encoder_offline = false;
    bool over_voltage = false;
    bool under_voltage = false;
    bool over_current = false;

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(velocity));
      a->Visit(MJ_NVP(position));
      a->Visit(MJ_NVP(hall_offset));
      a->Visit(MJ_NVP(hall_value));
      a->Visit(MJ_NVP(status_code));
      a->Visit(MJ_NVP(errors_code));
      a->Visit(MJ_NVP(board_ntc));
      a->Visit(MJ_NVP(iq_current));
      a->Visit(MJ_NVP(switched_on));
      a->Visit(MJ_NVP(target_reached));
      a->Visit(MJ_NVP(current_limit_active));
      a->Visit(MJ_NVP(adc_selftest_fatal));
      a->Visit(MJ_NVP(encoder_offline));
      a->Visit(MJ_NVP(over_voltage));
      a->Visit(MJ_NVP(under_voltage));
      a->Visit(MJ_NVP(over_current));
    }
  };

  struct Config {
    bool enable = false;
    int32_t invert_motor_dir = 0;
    float inertia = 0.0f;
    float torque_constant = 0.0f;
    int32_t motor_pole_pairs = 0;
    float motor_phase_resistance = 0.0f;
    float motor_phase_inductance = 0.0f;
    float current_limit = 0.0f;
    float velocity_limit = 0.0f;
    float calib_current = 0.0f;
    float calib_voltage = 0.0f;
    int32_t control_mode = 0;
    float pos_gain = 0.0f;
    float vel_gain = 0.0f;
    float vel_integrator_gain = 0.0f;
    float current_ctrl_bw = 0.0f;
    int32_t anticogging_enable = 0;
    int32_t sync_target_enable = 0;
    float target_velocity_window = 0.0f;
    float target_position_window = 0.0f;
    float torque_ramp_rate = 0.0f;
    float velocity_ramp_rate = 0.0f;
    float position_filter_bw = 0.0f;
    float profile_velocity = 0.0f;
    float profile_accel = 0.0f;
    float profile_decel = 0.0f;
    float protect_under_voltage = 0.0f;
    float protect_over_voltage = 0.0f;
    float protect_over_current = 0.0f;
    float protect_i_bus_max = 0.0f;
    int32_t node_id = 0;
    int32_t can_baudrate = 0;
    int32_t heartbeat_consumer_ms = 0;
    int32_t heartbeat_producer_ms = 0;
    int32_t calib_valid = 0;
    int32_t encoder_dir = 0;
    int32_t encoder_offset = 0;
    int32_t offset_lut = 0;

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(enable));
      a->Visit(MJ_NVP(invert_motor_dir));
      a->Visit(MJ_NVP(inertia));
      a->Visit(MJ_NVP(torque_constant));
      a->Visit(MJ_NVP(motor_pole_pairs));
      a->Visit(MJ_NVP(motor_phase_resistance));
      a->Visit(MJ_NVP(motor_phase_inductance));
      a->Visit(MJ_NVP(current_limit));
      a->Visit(MJ_NVP(velocity_limit));
      a->Visit(MJ_NVP(calib_current));
      a->Visit(MJ_NVP(calib_voltage));
      a->Visit(MJ_NVP(control_mode));
      a->Visit(MJ_NVP(pos_gain));
      a->Visit(MJ_NVP(vel_gain));
      a->Visit(MJ_NVP(vel_integrator_gain));
      a->Visit(MJ_NVP(current_ctrl_bw));
      a->Visit(MJ_NVP(anticogging_enable));
      a->Visit(MJ_NVP(sync_target_enable));
      a->Visit(MJ_NVP(target_velocity_window));
      a->Visit(MJ_NVP(target_position_window));
      a->Visit(MJ_NVP(torque_ramp_rate));
      a->Visit(MJ_NVP(velocity_ramp_rate));
      a->Visit(MJ_NVP(position_filter_bw));
      a->Visit(MJ_NVP(profile_velocity));
      a->Visit(MJ_NVP(profile_accel));
      a->Visit(MJ_NVP(profile_decel));
      a->Visit(MJ_NVP(protect_under_voltage));
      a->Visit(MJ_NVP(protect_over_voltage));
      a->Visit(MJ_NVP(protect_over_current));
      a->Visit(MJ_NVP(protect_i_bus_max));
      a->Visit(MJ_NVP(node_id));
      a->Visit(MJ_NVP(can_baudrate));
      a->Visit(MJ_NVP(heartbeat_consumer_ms));
      a->Visit(MJ_NVP(heartbeat_producer_ms));
      a->Visit(MJ_NVP(calib_valid));
      a->Visit(MJ_NVP(encoder_dir));
      a->Visit(MJ_NVP(encoder_offset));
      a->Visit(MJ_NVP(offset_lut));
    }
  };

  Status* status() { return &status_; }
  const Status* status() const { return &status_; }
  Config* config() { return &config_; }
  const Config* config() const { return &config_; }

  template <typename Archive>
  void Serialize(Archive* a) {
    status_.Serialize(a);
  }

 private:
  Status status_;
  Config config_;
};

}
