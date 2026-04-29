#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mbed.h>

#include "fw/bldc_servo.h"
#include "fw/error.h"
#include "fw/fdcan.h"
#include "fw/moteus_hw.h"
#include "fw/torque_model.h"
#include "mjlib/micro/command_manager.h"
#include "mjlib/multiplex/micro_server.h"
// using mjlib::micro::CommandManager;

namespace moteus {

class CustomProtocol {
public:
  CustomProtocol(mjlib::multiplex::MicroServer *multiplex_protocol,
                 BldcServo *bldc_servo, FDCan *fdcan,
                 mjlib::micro::PersistentConfig *persistent_config)
      : multiplex_protocol_(multiplex_protocol), bldc_servo_(bldc_servo),
        fdcan_(fdcan), persistent_config_(persistent_config) {}

  static bool CallbackTrampoline(uint32_t can_id, int dlc, const char *data,
                                 void *context) {
    return static_cast<CustomProtocol *>(context)->HandleFrame(can_id, dlc,
                                                               data);
  }

  void Init() {
    if (bldc_servo_ == nullptr || multiplex_protocol_ == nullptr) {
      return;
    }

    const auto &servo_config = bldc_servo_->config();
    const auto &motor = bldc_servo_->motor();
    const auto &torque_model = bldc_servo_->torque_model();
    const auto *position_config = bldc_servo_->motor_position_config();

    config_.invert_motor_dir =
        position_config != nullptr ? position_config->output.sign : 0;
    config_.inertia = servo_config.inertia_feedforward;
    config_.torque_constant = torque_model.torque_constant_;
    config_.motor_pole_pairs = motor.poles / 2;
    config_.motor_phase_resistance = motor.resistance_ohm;
    config_.motor_phase_inductance = motor.inductance_d_H;
    config_.current_limit = servo_config.max_current_A;
    config_.velocity_limit = servo_config.max_velocity;
    config_.calib_current = 0.0f;
    config_.calib_voltage = 0.0f;
    config_.control_mode = 0;
    config_.pos_gain = servo_config.pid_position.kp;
    config_.vel_gain = servo_config.pid_position.kd;
    config_.vel_integrator_gain = servo_config.pid_position.ki;
    config_.current_ctrl_bw = servo_config.pid_dq_hz;
    config_.anticogging_enable = motor.cogging_dq_scale != 0.0f;
    config_.sync_target_enable = 0;
    config_.target_velocity_window =
        CleanFloat(servo_config.fault_velocity_error);
    config_.target_position_window =
        CleanFloat(servo_config.fault_position_error);
    config_.torque_ramp_rate = servo_config.max_current_desired_rate;
    config_.velocity_ramp_rate = servo_config.default_accel_limit;
    config_.position_filter_bw = 0.0f;
    config_.profile_velocity = CleanFloat(servo_config.default_velocity_limit);
    config_.profile_accel = servo_config.default_accel_limit;
    config_.profile_decel = servo_config.default_accel_limit;
    config_.protect_under_voltage = 0.0f;
    config_.protect_over_voltage = servo_config.max_voltage;
    config_.protect_over_current = servo_config.max_current_A;
    config_.protect_i_bus_max = CleanFloat(servo_config.max_regen_power_W);
    config_.node_id = multiplex_protocol_->config()->id;
    config_.can_baudrate = fdcan_ != nullptr ? fdcan_->fast_bitrate() : 0;
    config_.heartbeat_consumer_ms = 0;
    config_.heartbeat_producer_ms = 0;
    config_.calib_valid = 0;

    if (position_config != nullptr) {
      config_.encoder_dir = position_config->output.sign;
      config_.encoder_offset =
          static_cast<int32_t>(position_config->output.offset);
    } else {
      config_.encoder_dir = 0;
      config_.encoder_offset = 0;
    }
    config_.offset_lut = 0;
  }

  void PollMillisecond() {
    if (ms_since_last_send_ < std::numeric_limits<uint32_t>::max()) {
      ms_since_last_send_++;
    }

    PollStatuswordReport();

    if (!auto_value_1_enabled_) {
      PollHeartbeatProducer();
    } else {
      HandleGetValue1(0, nullptr);
    }
  }

private:
  static constexpr int8_t DlcAny = -1;
  static constexpr int8_t DlcNotUsed = -2;

  static constexpr uint8_t BroadcastAddress = 0x1F;

  static constexpr uint8_t DirOffset = 10;
  static constexpr uint8_t NodeOffset = 5;
  static constexpr uint8_t CmdOffset = 0;
  static constexpr uint32_t DefaultHeartbeatProducerMs = 4000;

  static float CleanFloat(float value) {
    return std::isnan(value) ? 0.0f : value;
  }

  static uint32_t ToRaw(float value) {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static uint32_t ToRaw(int32_t value) {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static float ToFloat(uint32_t value) {
    float result = 0.0f;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static int32_t ToInt32(uint32_t value) {
    int32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  enum Dir : uint8_t {
    Receive = 0,
    Send = 1,
  };

  enum CmdId : uint8_t {
    CAN_CMD_MOTOR_DISABLE = 0,
    CAN_CMD_MOTOR_ENABLE = 1,
    CAN_CMD_SET_TORQUE = 2,
    CAN_CMD_SET_VELOCITY = 3,
    CAN_CMD_SET_POSITION = 4,
    CAN_CMD_CALIB_START = 5,
    CAN_CMD_CALIB_REPORT = 6,
    CAN_CMD_CALIB_ABORT = 7,
    CAN_CMD_ANTICOGGING_START = 8,
    CAN_CMD_ANTICOGGING_REPORT = 9,
    CAN_CMD_ANTICOGGING_ABORT = 10,
    CAN_CMD_SET_HOME = 11,
    CAN_CMD_ERROR_RESET = 12,
    CAN_CMD_GET_STATUSWORD = 13,
    CAN_CMD_STATUSWORD_REPORT = 14,
    CAN_CMD_GET_VALUE_1 = 15,
    CAN_CMD_GET_VALUE_2 = 16,
    CAN_CMD_SET_CONFIG = 17,
    CAN_CMD_GET_CONFIG = 18,
    CAN_CMD_SAVE_ALL_CONFIG = 19,
    CAN_CMD_RESET_ALL_CONFIG = 20,
    CAN_CMD_SYNC = 21,
    CAN_CMD_HEARTBEAT = 22,
    CAN_CMD_START_AUTO = 27,
    CAN_CMD_GET_FW_VERSION = 28,
    CAN_CMD_DFU_START = 29,
    CAN_CMD_DFU_DATA = 30,
    CAN_CMD_DFU_END = 31,
    CAN_CMD_COUNT = 32,
  };

  enum ConfigId : uint8_t {
    CONFIG_INVERT_MOTOR_DIR = 1,
    CONFIG_INERTIA = 2,
    CONFIG_TORQUE_CONSTANT = 3,
    CONFIG_MOTOR_POLE_PAIRS = 4,
    CONFIG_MOTOR_PHASE_RESISTANCE = 5,
    CONFIG_MOTOR_PHASE_INDUCTANCE = 6,
    CONFIG_CURRENT_LIMIT = 7,
    CONFIG_VELOCITY_LIMIT = 8,
    CONFIG_CALIB_CURRENT = 9,
    CONFIG_CALIB_VOLTAGE = 10,
    CONFIG_CONTROL_MODE = 11,
    CONFIG_POS_GAIN = 12,
    CONFIG_VEL_GAIN = 13,
    CONFIG_VEL_INTEGRATOR_GAIN = 14,
    CONFIG_CURRENT_CTRL_BW = 15,
    CONFIG_ANTICOGGING_ENABLE = 16,
    CONFIG_SYNC_TARGET_ENABLE = 17,
    CONFIG_TARGET_VELOCITY_WINDOW = 18,
    CONFIG_TARGET_POSITION_WINDOW = 19,
    CONFIG_TORQUE_RAMP_RATE = 20,
    CONFIG_VELOCITY_RAMP_RATE = 21,
    CONFIG_POSITION_FILTER_BW = 22,
    CONFIG_PROFILE_VELOCITY = 23,
    CONFIG_PROFILE_ACCEL = 24,
    CONFIG_PROFILE_DECEL = 25,
    CONFIG_PROTECT_UNDER_VOLTAGE = 26,
    CONFIG_PROTECT_OVER_VOLTAGE = 27,
    CONFIG_PROTECT_OVER_CURRENT = 28,
    CONFIG_PROTECT_I_BUS_MAX = 29,
    CONFIG_NODE_ID = 30,
    CONFIG_CAN_BAUDRATE = 31,
    CONFIG_HEARTBEAT_CONSUMER_MS = 32,
    CONFIG_HEARTBEAT_PRODUCER_MS = 33,
    CONFIG_CALIB_VALID = 34,
    CONFIG_ENCODER_DIR = 35,
    CONFIG_ENCODER_OFFSET = 36,
    CONFIG_OFFSET_LUT = 37,
    CONFIG_COUNT = 38,
  };

  struct ConfigParams {
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
  };

  struct CmdEntry {
    int8_t expected_dlc;
    bool (CustomProtocol::*handler)(int dlc, const char *data);
  };

  enum ProtocolControlMode : int32_t {
    ControlTorque = 0,
    ControlVelocity = 1,
    ControlFilteredPosition = 2,
    ControlProfiledPosition = 3,
  };

  enum StatusWord : uint32_t {
    StatusSwitchedOn = 1u << 0,
    StatusTargetReached = 1u << 1,
    StatusCurrentLimitActive = 1u << 2,
  };

  enum ErrorWord : uint32_t {
    ErrorAdcSelftestFatal = 1u << 0,
    ErrorEncoderOffline = 1u << 1,
    ErrorOverVoltage = 1u << 16,
    ErrorUnderVoltage = 1u << 17,
    ErrorOverCurrent = 1u << 18,
    ErrorOther = 1u << 19,
  };

  uint32_t MakeStatusCode() const {
    if (bldc_servo_ == nullptr) {
      return 0;
    }

    const auto &status = bldc_servo_->status();
    uint32_t result = 0;
    if (status.mode == BldcServo::Mode::kPosition) {
      result |= StatusSwitchedOn;
    }
    if (status.trajectory_done) {
      result |= StatusTargetReached;
    }
    if (status.fault == errc::kLimitMaxCurrent) {
      result |= StatusCurrentLimitActive;
    }
    return result;
  }

  uint32_t MakeErrorsCode() const {
    if (bldc_servo_ == nullptr) {
      return 0;
    }

    switch (bldc_servo_->status().fault) {
    case errc::kOverVoltage:
      return ErrorOverVoltage;
    case errc::kUnderVoltage:
      return ErrorUnderVoltage;
    case errc::kLimitMaxCurrent:
      return ErrorOverCurrent;
    case errc::kEncoderFault:
      return ErrorEncoderOffline;
    case errc::kSuccess:
      return 0;
    default:
      return ErrorOther;
    }
  }

  bool SendFrame(uint32_t can_id, int dlc, const char *data) {
    if (fdcan_ == nullptr || dlc < 0 || dlc > 64) {
      return false;
    }
    FDCan::SendOptions options;
    options.fdcan_frame = FDCan::Override::kRequire;
    options.bitrate_switch = FDCan::Override::kRequire;
    const char empty = 0;
    fdcan_->Send(can_id, std::string_view(dlc == 0 ? &empty : data, dlc),
                 options);
    ms_since_last_send_ = 0;
    return true;
  }

  void PollHeartbeatProducer() {
    const uint32_t producer_ms =
        config_.heartbeat_producer_ms > 0
            ? static_cast<uint32_t>(config_.heartbeat_producer_ms)
            : DefaultHeartbeatProducerMs;
    if (ms_since_last_send_ < producer_ms) {
      return;
    }

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_HEARTBEAT,
              0, nullptr);
  }

  void PollStatuswordReport() {
    if (bldc_servo_ == nullptr) {
      return;
    }

    const uint32_t status_code = MakeStatusCode();
    const uint32_t errors_code = MakeErrorsCode();
    if (!statusword_report_initialized_) {
      status_ = status_code;
      errors_ = errors_code;
      statusword_report_initialized_ = true;
      return;
    }
    if (status_ == status_code && errors_ == errors_code) {
      return;
    }

    status_ = status_code;
    errors_ = errors_code;

    char reply[8] = {0};
    reply[0] = status_code & 0xFF;
    reply[1] = (status_code >> 8) & 0xFF;
    reply[2] = (status_code >> 16) & 0xFF;
    reply[3] = (status_code >> 24) & 0xFF;
    reply[4] = errors_code & 0xFF;
    reply[5] = (errors_code >> 8) & 0xFF;
    reply[6] = (errors_code >> 16) & 0xFF;
    reply[7] = (errors_code >> 24) & 0xFF;

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_STATUSWORD_REPORT,
              8, reply);
  }

  void ApplyConfigSideEffects(uint32_t index) {
    if (bldc_servo_ != nullptr) {
      auto &servo_config =
          const_cast<BldcServo::Config &>(bldc_servo_->config());
      auto &motor = const_cast<BldcServo::Motor &>(bldc_servo_->motor());
      auto &torque_model =
          const_cast<TorqueModel &>(bldc_servo_->torque_model());
      auto *position_config = bldc_servo_->motor_position_config();

      switch (index) {
      case CONFIG_INVERT_MOTOR_DIR: {
        if (position_config != nullptr) {
          position_config->output.sign = config_.invert_motor_dir;
        }
        break;
      }
      case CONFIG_INERTIA: {
        servo_config.inertia_feedforward = config_.inertia;
        break;
      }
      case CONFIG_TORQUE_CONSTANT: {
        torque_model = TorqueModel(
            config_.torque_constant, motor.rotation_current_cutoff_A,
            motor.rotation_current_scale, motor.rotation_torque_scale);
        break;
      }
      case CONFIG_MOTOR_POLE_PAIRS: {
        motor.poles = config_.motor_pole_pairs * 2;
        break;
      }
      case CONFIG_MOTOR_PHASE_RESISTANCE: {
        motor.resistance_ohm = config_.motor_phase_resistance;
        break;
      }
      case CONFIG_MOTOR_PHASE_INDUCTANCE: {
        motor.inductance_d_H = config_.motor_phase_inductance;
        motor.inductance_q_H = config_.motor_phase_inductance;
        break;
      }
      case CONFIG_CURRENT_LIMIT: {
        servo_config.max_current_A = config_.current_limit;
        break;
      }
      case CONFIG_VELOCITY_LIMIT: {
        servo_config.max_velocity = config_.velocity_limit;
        break;
      }
      case CONFIG_POS_GAIN: {
        servo_config.pid_position.kp = config_.pos_gain;
        break;
      }
      case CONFIG_VEL_GAIN: {
        servo_config.pid_position.kd = config_.vel_gain;
        break;
      }
      case CONFIG_VEL_INTEGRATOR_GAIN: {
        servo_config.pid_position.ki = config_.vel_integrator_gain;
        break;
      }
      case CONFIG_CURRENT_CTRL_BW: {
        servo_config.pid_dq_hz = config_.current_ctrl_bw;
        break;
      }
      case CONFIG_ANTICOGGING_ENABLE: {
        motor.cogging_dq_scale = config_.anticogging_enable ? 1.0f : 0.0f;
        break;
      }
      case CONFIG_TARGET_VELOCITY_WINDOW: {
        servo_config.fault_velocity_error = config_.target_velocity_window;
        break;
      }
      case CONFIG_TARGET_POSITION_WINDOW: {
        servo_config.fault_position_error = config_.target_position_window;
        break;
      }
      case CONFIG_TORQUE_RAMP_RATE: {
        servo_config.max_current_desired_rate = config_.torque_ramp_rate;
        break;
      }
      case CONFIG_VELOCITY_RAMP_RATE:
      case CONFIG_PROFILE_ACCEL:
      case CONFIG_PROFILE_DECEL: {
        servo_config.default_accel_limit = config_.velocity_ramp_rate;
        break;
      }
      case CONFIG_PROFILE_VELOCITY: {
        servo_config.default_velocity_limit = config_.profile_velocity;
        break;
      }
      case CONFIG_PROTECT_OVER_VOLTAGE: {
        servo_config.max_voltage = config_.protect_over_voltage;
        break;
      }
      case CONFIG_PROTECT_OVER_CURRENT: {
        servo_config.max_current_A = config_.protect_over_current;
        break;
      }
      case CONFIG_PROTECT_I_BUS_MAX: {
        servo_config.max_regen_power_W = config_.protect_i_bus_max;
        break;
      }
      case CONFIG_NODE_ID: {
        if (multiplex_protocol_ != nullptr && config_.node_id >= 1 &&
            config_.node_id <= 126) {
          multiplex_protocol_->config()->id = config_.node_id;
        }
        break;
      }
      case CONFIG_ENCODER_DIR: {
        if (position_config != nullptr) {
          position_config->output.sign = config_.encoder_dir;
        }
        break;
      }
      case CONFIG_ENCODER_OFFSET: {
        if (position_config != nullptr) {
          position_config->output.offset = config_.encoder_offset;
        }
        break;
      }
      default: {
        break;
      }
      }
    }

    if (index == CONFIG_CAN_BAUDRATE && fdcan_ != nullptr &&
        config_.can_baudrate > 0) {
      fdcan_->SetFastBitrate(config_.can_baudrate);
    }
  }

  bool SetConfigRaw(uint32_t index, uint32_t raw_value) {
    switch (index) {
    case CONFIG_INVERT_MOTOR_DIR:
      config_.invert_motor_dir = ToInt32(raw_value);
      break;
    case CONFIG_INERTIA:
      config_.inertia = ToFloat(raw_value);
      break;
    case CONFIG_TORQUE_CONSTANT:
      config_.torque_constant = ToFloat(raw_value);
      break;
    case CONFIG_MOTOR_POLE_PAIRS:
      config_.motor_pole_pairs = ToInt32(raw_value);
      break;
    case CONFIG_MOTOR_PHASE_RESISTANCE:
      config_.motor_phase_resistance = ToFloat(raw_value);
      break;
    case CONFIG_MOTOR_PHASE_INDUCTANCE:
      config_.motor_phase_inductance = ToFloat(raw_value);
      break;
    case CONFIG_CURRENT_LIMIT:
      config_.current_limit = ToFloat(raw_value);
      break;
    case CONFIG_VELOCITY_LIMIT:
      config_.velocity_limit = ToFloat(raw_value);
      break;
    case CONFIG_CALIB_CURRENT:
      config_.calib_current = ToFloat(raw_value);
      break;
    case CONFIG_CALIB_VOLTAGE:
      config_.calib_voltage = ToFloat(raw_value);
      break;
    case CONFIG_CONTROL_MODE:
      config_.control_mode = ToInt32(raw_value);
      break;
    case CONFIG_POS_GAIN:
      config_.pos_gain = ToFloat(raw_value);
      break;
    case CONFIG_VEL_GAIN:
      config_.vel_gain = ToFloat(raw_value);
      break;
    case CONFIG_VEL_INTEGRATOR_GAIN:
      config_.vel_integrator_gain = ToFloat(raw_value);
      break;
    case CONFIG_CURRENT_CTRL_BW:
      config_.current_ctrl_bw = ToFloat(raw_value);
      break;
    case CONFIG_ANTICOGGING_ENABLE:
      config_.anticogging_enable = ToInt32(raw_value);
      break;
    case CONFIG_SYNC_TARGET_ENABLE:
      config_.sync_target_enable = ToInt32(raw_value);
      break;
    case CONFIG_TARGET_VELOCITY_WINDOW:
      config_.target_velocity_window = ToFloat(raw_value);
      break;
    case CONFIG_TARGET_POSITION_WINDOW:
      config_.target_position_window = ToFloat(raw_value);
      break;
    case CONFIG_TORQUE_RAMP_RATE:
      config_.torque_ramp_rate = ToFloat(raw_value);
      break;
    case CONFIG_VELOCITY_RAMP_RATE:
      config_.velocity_ramp_rate = ToFloat(raw_value);
      break;
    case CONFIG_POSITION_FILTER_BW:
      config_.position_filter_bw = ToFloat(raw_value);
      break;
    case CONFIG_PROFILE_VELOCITY:
      config_.profile_velocity = ToFloat(raw_value);
      break;
    case CONFIG_PROFILE_ACCEL:
      config_.profile_accel = ToFloat(raw_value);
      break;
    case CONFIG_PROFILE_DECEL:
      config_.profile_decel = ToFloat(raw_value);
      break;
    case CONFIG_PROTECT_UNDER_VOLTAGE:
      config_.protect_under_voltage = ToFloat(raw_value);
      break;
    case CONFIG_PROTECT_OVER_VOLTAGE:
      config_.protect_over_voltage = ToFloat(raw_value);
      break;
    case CONFIG_PROTECT_OVER_CURRENT:
      config_.protect_over_current = ToFloat(raw_value);
      break;
    case CONFIG_PROTECT_I_BUS_MAX:
      config_.protect_i_bus_max = ToFloat(raw_value);
      break;
    case CONFIG_NODE_ID:
      config_.node_id = ToInt32(raw_value);
      break;
    case CONFIG_CAN_BAUDRATE: {
      config_.can_baudrate = ToInt32(raw_value);
      if (config_.can_baudrate <= 0) {
        return false;
      }
      break;
    }
    case CONFIG_HEARTBEAT_CONSUMER_MS:
      config_.heartbeat_consumer_ms = ToInt32(raw_value);
      break;
    case CONFIG_HEARTBEAT_PRODUCER_MS:
      config_.heartbeat_producer_ms = ToInt32(raw_value);
      break;
    case CONFIG_CALIB_VALID:
      config_.calib_valid = ToInt32(raw_value);
      break;
    case CONFIG_ENCODER_DIR:
      config_.encoder_dir = ToInt32(raw_value);
      break;
    case CONFIG_ENCODER_OFFSET:
      config_.encoder_offset = ToInt32(raw_value);
      break;
    case CONFIG_OFFSET_LUT:
      config_.offset_lut = ToInt32(raw_value);
      break;
    default:
      return false;
    }

    ApplyConfigSideEffects(index);
    return true;
  }

  bool GetConfigRaw(uint32_t index, uint32_t *raw_value) const {
    switch (index) {
    case CONFIG_INVERT_MOTOR_DIR:
      *raw_value = ToRaw(config_.invert_motor_dir);
      break;
    case CONFIG_INERTIA:
      *raw_value = ToRaw(config_.inertia);
      break;
    case CONFIG_TORQUE_CONSTANT:
      *raw_value = ToRaw(config_.torque_constant);
      break;
    case CONFIG_MOTOR_POLE_PAIRS:
      *raw_value = ToRaw(config_.motor_pole_pairs);
      break;
    case CONFIG_MOTOR_PHASE_RESISTANCE:
      *raw_value = ToRaw(config_.motor_phase_resistance);
      break;
    case CONFIG_MOTOR_PHASE_INDUCTANCE:
      *raw_value = ToRaw(config_.motor_phase_inductance);
      break;
    case CONFIG_CURRENT_LIMIT:
      *raw_value = ToRaw(config_.current_limit);
      break;
    case CONFIG_VELOCITY_LIMIT:
      *raw_value = ToRaw(config_.velocity_limit);
      break;
    case CONFIG_CALIB_CURRENT:
      *raw_value = ToRaw(config_.calib_current);
      break;
    case CONFIG_CALIB_VOLTAGE:
      *raw_value = ToRaw(config_.calib_voltage);
      break;
    case CONFIG_CONTROL_MODE:
      *raw_value = ToRaw(config_.control_mode);
      break;
    case CONFIG_POS_GAIN:
      *raw_value = ToRaw(config_.pos_gain);
      break;
    case CONFIG_VEL_GAIN:
      *raw_value = ToRaw(config_.vel_gain);
      break;
    case CONFIG_VEL_INTEGRATOR_GAIN:
      *raw_value = ToRaw(config_.vel_integrator_gain);
      break;
    case CONFIG_CURRENT_CTRL_BW:
      *raw_value = ToRaw(config_.current_ctrl_bw);
      break;
    case CONFIG_ANTICOGGING_ENABLE:
      *raw_value = ToRaw(config_.anticogging_enable);
      break;
    case CONFIG_SYNC_TARGET_ENABLE:
      *raw_value = ToRaw(config_.sync_target_enable);
      break;
    case CONFIG_TARGET_VELOCITY_WINDOW:
      *raw_value = ToRaw(config_.target_velocity_window);
      break;
    case CONFIG_TARGET_POSITION_WINDOW:
      *raw_value = ToRaw(config_.target_position_window);
      break;
    case CONFIG_TORQUE_RAMP_RATE:
      *raw_value = ToRaw(config_.torque_ramp_rate);
      break;
    case CONFIG_VELOCITY_RAMP_RATE:
      *raw_value = ToRaw(config_.velocity_ramp_rate);
      break;
    case CONFIG_POSITION_FILTER_BW:
      *raw_value = ToRaw(config_.position_filter_bw);
      break;
    case CONFIG_PROFILE_VELOCITY:
      *raw_value = ToRaw(config_.profile_velocity);
      break;
    case CONFIG_PROFILE_ACCEL:
      *raw_value = ToRaw(config_.profile_accel);
      break;
    case CONFIG_PROFILE_DECEL:
      *raw_value = ToRaw(config_.profile_decel);
      break;
    case CONFIG_PROTECT_UNDER_VOLTAGE:
      *raw_value = ToRaw(config_.protect_under_voltage);
      break;
    case CONFIG_PROTECT_OVER_VOLTAGE:
      *raw_value = ToRaw(config_.protect_over_voltage);
      break;
    case CONFIG_PROTECT_OVER_CURRENT:
      *raw_value = ToRaw(config_.protect_over_current);
      break;
    case CONFIG_PROTECT_I_BUS_MAX:
      *raw_value = ToRaw(config_.protect_i_bus_max);
      break;
    case CONFIG_NODE_ID:
      *raw_value = ToRaw(config_.node_id);
      break;
    case CONFIG_CAN_BAUDRATE:
      *raw_value = ToRaw(config_.can_baudrate);
      break;
    case CONFIG_HEARTBEAT_CONSUMER_MS:
      *raw_value = ToRaw(config_.heartbeat_consumer_ms);
      break;
    case CONFIG_HEARTBEAT_PRODUCER_MS:
      *raw_value = ToRaw(config_.heartbeat_producer_ms);
      break;
    case CONFIG_CALIB_VALID:
      *raw_value = ToRaw(config_.calib_valid);
      break;
    case CONFIG_ENCODER_DIR:
      *raw_value = ToRaw(config_.encoder_dir);
      break;
    case CONFIG_ENCODER_OFFSET:
      *raw_value = ToRaw(config_.encoder_offset);
      break;
    case CONFIG_OFFSET_LUT:
      *raw_value = ToRaw(config_.offset_lut);
      break;
    default:
      return false;
    }
    return true;
  }

  bool HandleFrame(uint32_t can_id, int dlc, const char *data) {
    static DigitalOut debug_led_canfd(PB_15, 0);

    const uint8_t dir = (can_id >> DirOffset) & 0x01;
    if (dir != Receive) {
      return false;
    }

    const uint8_t node_id = (can_id >> NodeOffset) & 0x1F;
    if ((node_id != multiplex_protocol_->config()->id) &&
        (node_id != BroadcastAddress)) {
      return false;
    }

    const uint8_t cmd_id = (can_id >> CmdOffset) & 0x1F;
    if (cmd_id >= CAN_CMD_COUNT) {
      return false;
    }

    const CmdEntry &entry = command_table[cmd_id];
    if (entry.handler == nullptr) {
      return false;
    }

    if (entry.expected_dlc == DlcNotUsed) {
      return false;
    }
    if (entry.expected_dlc >= 0 && dlc != entry.expected_dlc) {
      return false;
    }
    debug_led_canfd = 1;

    (this->*(entry.handler))(dlc, data);

    debug_led_canfd = 0;
    return true;
  }

  bool HandleMotorDisable(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      char reply[4] = {0xFF};
      reply[0] = 1;
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_MOTOR_DISABLE,
                4, reply);
      return false;
    }

    if (bldc_servo_->status().mode != BldcServo::Mode::kStopped) {
      static BldcServo::CommandData command;

      command.mode = BldcServo::Mode::kStopped;
      bldc_servo_->Command(command);
    }
    char reply[4] = {0};
    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_MOTOR_DISABLE,
              4, reply);
    return true;
  }

  bool HandleMotorEnable(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      char reply[4] = {0xFF};
      reply[0] = 1;
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_MOTOR_ENABLE,
                4, reply);
      return false;
    }
    if (bldc_servo_->status().mode == BldcServo::Mode::kFault) {
      char reply[4] = {0xFF};
      reply[0] = 1;
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_MOTOR_ENABLE,
                4, reply);
      return false;
    }
    static BldcServo::CommandData command;
    command.mode = BldcServo::Mode::kPosition;
    command.position = std::numeric_limits<float>::quiet_NaN();
    command.velocity = 0.0f;
    command.timeout_s = std::numeric_limits<float>::quiet_NaN();
    bldc_servo_->Command(command);

    if (bldc_servo_->status().mode != BldcServo::Mode::kPosition) {
      int32_t fault_value = static_cast<int32_t>(bldc_servo_->status().fault);
      char reply[4] = {0xFF};
      reply[0] = fault_value & 0xFF;
      reply[1] = (fault_value >> 8) & 0xFF;
      reply[2] = (fault_value >> 16) & 0xFF;
      reply[3] = (fault_value >> 24) & 0xFF;
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_MOTOR_ENABLE,
                4, reply);
      return false;
    } else {
      char reply[4] = {0};
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_MOTOR_ENABLE,
                4, reply);
      return true;
    }
    return false;
  }

  bool HandleSetTorque(int dlc, const char *data) {
    std::memcpy(&pending_.feedforward_Nm, data, sizeof(float));
    if (config_.sync_target_enable == 0) {
      if (bldc_servo_ == nullptr)
        return false;
      if (bldc_servo_->status().mode != BldcServo::Mode::kPosition)
        return false;
      bldc_servo_->Command(pending_);
    }
    return true;
  }

  bool HandleSetVelocity(int dlc, const char *data) {
    std::memcpy(&pending_.velocity, data, sizeof(float));
    if (config_.sync_target_enable == 0) {
      if (bldc_servo_ == nullptr)
        return false;
      if (bldc_servo_->status().mode != BldcServo::Mode::kPosition)
        return false;
      pending_.position = std::numeric_limits<float>::quiet_NaN();
      pending_.feedforward_Nm = std::numeric_limits<float>::quiet_NaN();
      pending_.timeout_s = std::numeric_limits<float>::quiet_NaN();
      bldc_servo_->Command(pending_);
    }
    return true;
  }

  bool HandleSetPosition(int dlc, const char *data) {
    std::memcpy(&pending_.position, data, sizeof(float));
    if (config_.sync_target_enable == 0) {
      if (bldc_servo_ == nullptr)
        return false;
      if (bldc_servo_->status().mode != BldcServo::Mode::kPosition)
        return false;
      bldc_servo_->Command(pending_);
    }
    return true;
  }

  bool HandleSync(int dlc, const char *data) {
    if (config_.sync_target_enable == 0) {
      return true;
    }
    if (bldc_servo_ == nullptr)
      return false;
    if (bldc_servo_->status().mode != BldcServo::Mode::kPosition)
      return false;
    bldc_servo_->Command(pending_);
    return true;
  }

  bool HandleSetHome(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      char reply[4] = {0xFF};
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_SET_HOME,
                4, reply);
      return false;
    }

    auto *const config = bldc_servo_->motor_position_config();
    if (config == nullptr) {
      char reply[4] = {0xFF};
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_SET_HOME,
                4, reply);
      return false;
    }

    const float set_value = 0.0f;
    config->output.offset = 0.0f;
    bldc_servo_->SetOutputPositionNearest(set_value);

    const float cur_output = bldc_servo_->motor_position().position;
    const float error = set_value - cur_output;

    config->output.offset += error * config->output.sign;

    bldc_servo_->SetOutputPositionNearest(set_value);

    char reply[4] = {0};
    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_SET_HOME,
              4, reply);
    return true;
  }

  bool HandleErrorReset(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      char reply[4] = {0xFF};
      reply[0] = 1;
      SendFrame(Send << DirOffset |
                    (multiplex_protocol_->config()->id << NodeOffset) |
                    CAN_CMD_ERROR_RESET,
                4, reply);
      return false;
    }

    if (bldc_servo_->status().mode != BldcServo::Mode::kStopped) {
      static BldcServo::CommandData command;

      command.mode = BldcServo::Mode::kStopped;
      bldc_servo_->Command(command);
    }

    char reply[4] = {0};
    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_ERROR_RESET,
              4, reply);
    return true;
  }

  bool HandleGetStatusword(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      return false;
    }

    char reply[8] = {0};
    reply[0] = status_ & 0xFF;
    reply[1] = (status_ >> 8) & 0xFF;
    reply[2] = (status_ >> 16) & 0xFF;
    reply[3] = (status_ >> 24) & 0xFF;
    reply[4] = errors_ & 0xFF;
    reply[5] = (errors_ >> 8) & 0xFF;
    reply[6] = (errors_ >> 16) & 0xFF;
    reply[7] = (errors_ >> 24) & 0xFF;

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_GET_STATUSWORD,
              8, reply);
    return true;
  }

  bool HandleGetFwVersion(int dlc, const char *data) {
    char reply[8] = {0};
    reply[4] = MOTEUS_FIRMWARE_VERSION & 0xFF;
    reply[5] = (MOTEUS_FIRMWARE_VERSION >> 8) & 0xFF;
    reply[6] = (MOTEUS_FIRMWARE_VERSION >> 16) & 0xFF;
    reply[7] = (MOTEUS_FIRMWARE_VERSION >> 24) & 0xFF;

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_GET_FW_VERSION,
              8, reply);
    return true;
  }

  bool HandleCalibStart(int dlc, const char *data) { return false; }
  bool HandleCalibAbort(int dlc, const char *data) { return false; }
  bool HandleAnticoggingStart(int dlc, const char *data) { return false; }
  bool HandleAnticoggingAbort(int dlc, const char *data) { return false; }
  bool HandleGetValue1(int dlc, const char *data) {
    if (bldc_servo_ == nullptr) {
      return false;
    }

    const auto &s = bldc_servo_->status();
    const auto &mp = bldc_servo_->motor_position();

    // velocity: rev/s → int16
    const int16_t vel_i16 = static_cast<int16_t>(s.velocity);
    // position: rev → int16
    const int16_t pos_i16 = static_cast<int16_t>(s.position);
    // hall offset: low 16 bits of offset_value
    const uint16_t hall_offset =
        static_cast<uint16_t>(mp.sources[0].offset_value & 0xFFFF);
    // hall value: raw sensor value
    const uint16_t hall_value =
        static_cast<uint16_t>(mp.sources[0].raw & 0xFFFF);
    const uint16_t status_code = static_cast<uint16_t>(status_);
    const uint16_t errors_code = static_cast<uint16_t>(errors_);
    // board NTC: °C × 100 → int16
    const int16_t ntc_i16 = static_cast<int16_t>(s.fet_temp_C * 100.0f);
    // Iq current: A × 100 → int16
    const int16_t iq_i16 = static_cast<int16_t>(s.q_A * 1000.0f);

    char reply[16] = {0};
    reply[0] = vel_i16 & 0xFF;
    reply[1] = (vel_i16 >> 8) & 0xFF;
    reply[2] = pos_i16 & 0xFF;
    reply[3] = (pos_i16 >> 8) & 0xFF;
    reply[4] = hall_offset & 0xFF;
    reply[5] = (hall_offset >> 8) & 0xFF;
    reply[6] = hall_value & 0xFF;
    reply[7] = (hall_value >> 8) & 0xFF;
    reply[8] = status_code & 0xFF;
    reply[9] = (status_code >> 8) & 0xFF;
    reply[10] = errors_code & 0xFF;
    reply[11] = (errors_code >> 8) & 0xFF;
    reply[12] = ntc_i16 & 0xFF;
    reply[13] = (ntc_i16 >> 8) & 0xFF;
    reply[14] = iq_i16 & 0xFF;
    reply[15] = (iq_i16 >> 8) & 0xFF;

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_GET_VALUE_1,
              16, reply);
    return true;
  }

  bool HandleGetValue2(int dlc, const char *data) { return true; }
  bool HandleSetConfig(int dlc, const char *data) {
    uint32_t index = 0;
    uint32_t raw_value = 0;
    std::memcpy(&index, data, sizeof(index));
    std::memcpy(&raw_value, data + sizeof(index), sizeof(raw_value));

    if (!SetConfigRaw(index, raw_value)) {
      return false;
    }

    char reply[8] = {0};
    std::memcpy(reply, &index, sizeof(index));
    std::memcpy(reply + sizeof(index), &raw_value, sizeof(raw_value));
    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_SET_CONFIG,
              8, reply);
    return true;
  }

  bool HandleGetConfig(int dlc, const char *data) {
    uint32_t index = 0;
    uint32_t raw_value = 0;
    std::memcpy(&index, data, sizeof(index));

    if (!GetConfigRaw(index, &raw_value)) {
      return false;
    }

    char reply[8] = {0};
    std::memcpy(reply, &index, sizeof(index));
    std::memcpy(reply + sizeof(index), &raw_value, sizeof(raw_value));
    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_GET_CONFIG,
              8, reply);
    return true;
  }
  bool HandleSaveAllConfig(int dlc, const char *data) { return false; }
  bool HandleResetAllConfig(int dlc, const char *data) { return false; }
  bool HandleHeartbeat(int dlc, const char *data) { return true; }
  bool HandleStartAuto(int dlc, const char *data) {
    auto_value_1_enabled_ = (data[0] == 1);

    SendFrame(Send << DirOffset |
                  (multiplex_protocol_->config()->id << NodeOffset) |
                  CAN_CMD_START_AUTO,
              1, data);
    return true;
  }
  bool HandleDfuStart(int dlc, const char *data) { return false; }
  bool HandleDfuData(int dlc, const char *data) { return false; }
  bool HandleDfuEnd(int dlc, const char *data) { return false; }

  // Dispatch table: index = cmd_id.
  //   expected_dlc:
  //     >= 0          : exact match required
  //     DlcAny  (-1) : any length accepted (handler checks)
  //     DlcNotUsed(-2): not accepted from host (device->host only or reserved)
  //   handler == nullptr: unknown/unused slot
  static constexpr CmdEntry command_table[CAN_CMD_COUNT] = {
      /*  0 MOTOR_DISABLE      */ {0, &CustomProtocol::HandleMotorDisable},
      /*  1 MOTOR_ENABLE       */ {0, &CustomProtocol::HandleMotorEnable},
      /*  2 SET_TORQUE         */ {4, &CustomProtocol::HandleSetTorque},
      /*  3 SET_VELOCITY       */ {4, &CustomProtocol::HandleSetVelocity},
      /*  4 SET_POSITION       */ {4, &CustomProtocol::HandleSetPosition},
      /*  5 CALIB_START        */ {0, &CustomProtocol::HandleCalibStart},
      /*  6 CALIB_REPORT       */ {DlcNotUsed, nullptr},
      /*  7 CALIB_ABORT        */ {0, &CustomProtocol::HandleCalibAbort},
      /*  8 ANTICOGGING_START  */ {0, &CustomProtocol::HandleAnticoggingStart},
      /*  9 ANTICOGGING_REPORT */ {DlcNotUsed, nullptr},
      /* 10 ANTICOGGING_ABORT  */ {0, &CustomProtocol::HandleAnticoggingAbort},
      /* 11 SET_HOME           */ {0, &CustomProtocol::HandleSetHome},
      /* 12 ERROR_RESET        */ {0, &CustomProtocol::HandleErrorReset},
      /* 13 GET_STATUSWORD     */ {0, &CustomProtocol::HandleGetStatusword},
      /* 14 STATUSWORD_REPORT  */ {DlcNotUsed, nullptr},
      /* 15 GET_VALUE_1        */ {0, &CustomProtocol::HandleGetValue1},
      /* 16 GET_VALUE_2        */ {0, &CustomProtocol::HandleGetValue2},
      /* 17 SET_CONFIG         */ {8, &CustomProtocol::HandleSetConfig},
      /* 18 GET_CONFIG         */ {4, &CustomProtocol::HandleGetConfig},
      /* 19 SAVE_ALL_CONFIG    */ {0, &CustomProtocol::HandleSaveAllConfig},
      /* 20 RESET_ALL_CONFIG   */ {0, &CustomProtocol::HandleResetAllConfig},
      /* 21 SYNC               */ {0, &CustomProtocol::HandleSync},
      /* 22 HEARTBEAT          */ {0, &CustomProtocol::HandleHeartbeat},
      /* 23 reserved           */ {DlcNotUsed, nullptr},
      /* 24 reserved           */ {DlcNotUsed, nullptr},
      /* 25 reserved           */ {DlcNotUsed, nullptr},
      /* 26 reserved           */ {DlcNotUsed, nullptr},
      /* 27 START_AUTO         */ {1, &CustomProtocol::HandleStartAuto},
      /* 28 GET_FW_VERSION     */ {0, &CustomProtocol::HandleGetFwVersion},
      /* 29 DFU_START          */ {0, &CustomProtocol::HandleDfuStart},
      /* 30 DFU_DATA           */ {DlcAny, &CustomProtocol::HandleDfuData},
      /* 31 DFU_END            */ {8, &CustomProtocol::HandleDfuEnd},
  };

  mjlib::multiplex::MicroServer *const multiplex_protocol_;
  BldcServo *const bldc_servo_;
  FDCan *const fdcan_;
  mjlib::micro::PersistentConfig *const persistent_config_;

  BldcServo::CommandData pending_;
  ConfigParams config_;

  uint32_t status_ = 0;
  uint32_t errors_ = 0;

  bool statusword_report_initialized_ = false;
  bool auto_value_1_enabled_ = false;
  uint32_t ms_since_last_send_ = 0;

  // 速度                bldc_servo_->status().velocity
  // 位置                bldc_servo_->status().position
  // hall偏移量          bldc_servo_->motor_position().sources[0].offset_value
  // hall值              bldc_servo_->motor_position().sources[0].raw
  // status_code         bldc_servo_->status().mode
  // errors_code         bldc_servo_->status().fault
  // 板载NTC             bldc_servo_->status().fet_temp_C
  // Iq电流              bldc_servo_->status().q_A
};

} // namespace moteus
