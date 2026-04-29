#pragma once

#include <cstdint>

#include "mjlib/base/visitor.h"

namespace moteus {

struct Fuda {
  Fuda();

  struct Status {
    bool enabled = false;                 // 2.2 CMD 0/1, 2.3 bit0 switched_on
    float velocity = 0.0f;                // 2.2 CMD 15 GET_VALUE_1: 速度
    float position = 0.0f;                // 2.2 CMD 15 GET_VALUE_1: 位置
    int32_t hall_offset = 0;              // 2.2 CMD 15 GET_VALUE_1: hall偏移量
    int32_t hall_value = 0;               // 2.2 CMD 15 GET_VALUE_1: hall值
    uint32_t status_code = 0;             // 2.2 CMD 13/14, 2.3 状态字
    uint32_t errors_code = 0;             // 2.2 CMD 13/14, 2.4 错误字
    float board_ntc = 0.0f;               // 2.2 CMD 15 GET_VALUE_1: 板载NTC
    float iq_current = 0.0f;              // 2.2 CMD 15 GET_VALUE_1: Iq电流

    bool switched_on = false;             // 2.3 status_code bit0
    bool target_reached = false;          // 2.3 status_code bit1
    bool current_limit_active = false;    // 2.3 status_code bit2

    bool adc_selftest_fatal = false;      // 2.4 errors_code bit0
    bool encoder_offline = false;         // 2.4 errors_code bit1
    bool over_voltage = false;            // 2.4 errors_code bit16
    bool under_voltage = false;           // 2.4 errors_code bit17
    bool over_current = false;            // 2.4 errors_code bit18
    bool other_fault = false;             // 2.4 errors_code 保留/扩展错误

    bool calib_running = false;           // 2.2 CMD 5/7 校准启动/中止状态
    int32_t calib_step = 0;               // 2.2 CMD 6 CALIB_REPORT: step
    int32_t calib_data = 0;               // 2.2 CMD 6 CALIB_REPORT: data
    bool anticogging_running = false;     // 2.2 CMD 8/10 齿槽补偿启动/中止状态
    int32_t anticogging_step = 0;         // 2.2 CMD 9 ANTICOGGING_REPORT: step
    int32_t anticogging_value = 0;        // 2.2 CMD 9 ANTICOGGING_REPORT: value
    bool heartbeat_alive = false;         // 2.2 CMD 22 HEARTBEAT
    int32_t last_return_code = 0;         // 2.2 带回复命令: int32 返回值
    int32_t fw_version_major = 0;         // 2.2 CMD 28 GET_FW_VERSION: 主版本
    int32_t fw_version_minor = 0;         // 2.2 CMD 28 GET_FW_VERSION: 次版本
    bool auto_value_1_enabled = false;    // 2.2 CMD 27 START_AUTO 当前状态
    bool dfu_running = false;             // 2.2 CMD 29/31 DFU_START/END 状态

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(enabled));
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
      a->Visit(MJ_NVP(other_fault));
      a->Visit(MJ_NVP(calib_running));
      a->Visit(MJ_NVP(calib_step));
      a->Visit(MJ_NVP(calib_data));
      a->Visit(MJ_NVP(anticogging_running));
      a->Visit(MJ_NVP(anticogging_step));
      a->Visit(MJ_NVP(anticogging_value));
      a->Visit(MJ_NVP(heartbeat_alive));
      a->Visit(MJ_NVP(last_return_code));
      a->Visit(MJ_NVP(fw_version_major));
      a->Visit(MJ_NVP(fw_version_minor));
      a->Visit(MJ_NVP(auto_value_1_enabled));
      a->Visit(MJ_NVP(dfu_running));
    }
  };

  struct Config {
    bool enable = false;                  // Fuda扩展: 总使能
    float input_pos = 0.0f;               // 2.2 CMD 4 SET_POSITION: 位置给定
    float input_vel = 0.0f;               // 2.2 CMD 3 SET_VELOCITY: 速度给定
    float input_torque = 0.0f;            // 2.2 CMD 2 SET_TORQUE: 转矩给定
    bool calib_enable = false;            // 2.2 CMD 5/7 CALIB_START/ABORT 请求
    bool auto_value_1_enable = false;     // 2.2 CMD 27 START_AUTO 请求
    bool error_reset_request = false;     // 2.2 CMD 12 ERROR_RESET 请求
    bool set_home_request = false;        // 2.2 CMD 11 SET_HOME 请求
    bool save_all_config_request = false; // 2.2 CMD 19 SAVE_ALL_CONFIG 请求
    bool reset_all_config_request = false;// 2.2 CMD 20 RESET_ALL_CONFIG 请求
    bool dfu_enable = false;              // 2.2 CMD 29/31 DFU_START/END 请求


    int32_t invert_motor_dir = 0;         // 2.5 index 1 invert_motor_dir
    float inertia = 0.0f;                 // 2.5 index 2 inertia
    float torque_constant = 0.0f;         // 2.5 index 3 torque_constant
    int32_t motor_pole_pairs = 0;         // 2.5 index 4 motor_pole_pairs
    float motor_phase_resistance = 0.0f;  // 2.5 index 5 motor_phase_resistance
    float motor_phase_inductance = 0.0f;  // 2.5 index 6 motor_phase_inductance
    float current_limit = 0.0f;           // 2.5 index 7 current_limit
    float velocity_limit = 0.0f;          // 2.5 index 8 velocity_limit
    float calib_current = 0.0f;           // 2.5 index 9 calib_current
    float calib_voltage = 0.0f;           // 2.5 index 10 calib_voltage
    int32_t control_mode = 0;             // 2.5 index 11 control_mode
    float pos_gain = 0.0f;                // 2.5 index 12 pos_gain
    float vel_gain = 0.0f;                // 2.5 index 13 vel_gain
    float vel_integrator_gain = 0.0f;     // 2.5 index 14 vel_integrator_gain
    float current_ctrl_bw = 0.0f;         // 2.5 index 15 current_ctrl_bw
    int32_t anticogging_enable = 0;       // 2.5 index 16 anticogging_enable
    int32_t sync_target_enable = 0;       // 2.5 index 17 sync_target_enable
    float target_velocity_window = 0.0f;  // 2.5 index 18 target_velocity_window
    float target_position_window = 0.0f;  // 2.5 index 19 target_position_window
    float torque_ramp_rate = 0.0f;        // 2.5 index 20 torque_ramp_rate
    float velocity_ramp_rate = 0.0f;      // 2.5 index 21 velocity_ramp_rate
    float position_filter_bw = 0.0f;      // 2.5 index 22 position_filter_bw
    float profile_velocity = 0.0f;        // 2.5 index 23 profile_velocity
    float profile_accel = 0.0f;           // 2.5 index 24 profile_accel
    float profile_decel = 0.0f;           // 2.5 index 25 profile_decel
    float protect_under_voltage = 0.0f;   // 2.5 index 26 protect_under_voltage
    float protect_over_voltage = 0.0f;    // 2.5 index 27 protect_over_voltage
    float protect_over_current = 0.0f;    // 2.5 index 28 protect_over_current
    float protect_i_bus_max = 0.0f;       // 2.5 index 29 protect_i_bus_max
    int32_t node_id = 0;                  // 2.5 index 30 node_id
    int32_t can_baudrate = 0;             // 2.5 index 31 can_baudrate
    int32_t canfd_fastbaudrate = 0;       // Fuda扩展: CAN-FD data phase bitrate
    int32_t heartbeat_consumer_ms = 0;    // 2.5 index 32 heartbeat_consumer_ms
    int32_t heartbeat_producer_ms = 0;    // 2.5 index 33 heartbeat_producer_ms
    int32_t calib_valid = 0;              // 2.5 index 34 calib_valid
    int32_t encoder_dir = 0;              // 2.5 index 35 encoder_dir
    int32_t encoder_offset = 0;           // 2.5 index 36 encoder_offset
    int32_t offset_lut = 0;               // 2.5 index 37 offset_lut

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(enable));
      a->Visit(MJ_NVP(input_pos));
      a->Visit(MJ_NVP(input_vel));
      a->Visit(MJ_NVP(input_torque));
      a->Visit(MJ_NVP(calib_enable));
      a->Visit(MJ_NVP(auto_value_1_enable));
      a->Visit(MJ_NVP(error_reset_request));
      a->Visit(MJ_NVP(set_home_request));
      a->Visit(MJ_NVP(save_all_config_request));
      a->Visit(MJ_NVP(reset_all_config_request));
      a->Visit(MJ_NVP(dfu_enable));
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
      a->Visit(MJ_NVP(canfd_fastbaudrate));
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
