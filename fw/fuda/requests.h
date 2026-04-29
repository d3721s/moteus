#pragma once

#include <array>
#include <cstdint>

#include "mjlib/base/visitor.h"

namespace moteus {
namespace fuda {

struct Requests {
  struct EmptyCommand {
    template <typename Archive>
    void Serialize(Archive* a) { (void)a; }
  };

  struct SetTorque {
    float torque_Nm = 0.0f;  // 2.2 CMD 2 data: float 转矩 [Nm]

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(torque_Nm));
    }
  };

  struct SetVelocity {
    float velocity_turn_s = 0.0f;  // 2.2 CMD 3 data: float 速度 [turn/s]

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(velocity_turn_s));
    }
  };

  struct SetPosition {
    float position_turn = 0.0f;  // 2.2 CMD 4 data: float 位置 [turn]

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(position_turn));
    }
  };

  struct SetConfig {
    uint32_t index = 0;  // 2.2 CMD 17 data[0..3]: 配置索引
    uint32_t value = 0;  // 2.2 CMD 17 data[4..7]: uint32 值

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(index));
      a->Visit(MJ_NVP(value));
    }
  };

  struct GetConfig {
    uint32_t index = 0;  // 2.2 CMD 18 data[0..3]: 配置索引

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(index));
    }
  };

  struct StartAuto {
    uint8_t enable = 0;  // 2.2 CMD 27 data[0]: 1 开启, 0 关闭

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(enable));
    }
  };

  struct DfuData {
    uint8_t size = 0;                 // 2.2 CMD 30 DLC: 1~8
    std::array<uint8_t, 8> data = {{}};  // 2.2 CMD 30 data: DFU 数据包

    template <typename Archive>
    void Serialize(Archive* a) {
      a->Visit(MJ_NVP(size));
      a->Visit(MJ_NVP(data));
    }
  };

  EmptyCommand can_cmd_motor_disable;            // 2.2 CMD 0 CAN_CMD_MOTOR_DISABLE
  EmptyCommand can_cmd_motor_enable;             // 2.2 CMD 1 CAN_CMD_MOTOR_ENABLE
  SetTorque can_cmd_set_torque;                  // 2.2 CMD 2 CAN_CMD_SET_TORQUE
  SetVelocity can_cmd_set_velocity;              // 2.2 CMD 3 CAN_CMD_SET_VELOCITY
  SetPosition can_cmd_set_position;              // 2.2 CMD 4 CAN_CMD_SET_POSITION
  EmptyCommand can_cmd_calib_start;              // 2.2 CMD 5 CAN_CMD_CALIB_START
  EmptyCommand can_cmd_calib_report;             // 2.2 CMD 6 CAN_CMD_CALIB_REPORT
  EmptyCommand can_cmd_calib_abort;              // 2.2 CMD 7 CAN_CMD_CALIB_ABORT
  EmptyCommand can_cmd_anticogging_start;        // 2.2 CMD 8 CAN_CMD_ANTICOGGING_START
  EmptyCommand can_cmd_anticogging_report;       // 2.2 CMD 9 CAN_CMD_ANTICOGGING_REPORT
  EmptyCommand can_cmd_anticogging_abort;        // 2.2 CMD 10 CAN_CMD_ANTICOGGING_ABORT
  EmptyCommand can_cmd_set_home;                 // 2.2 CMD 11 CAN_CMD_SET_HOME
  EmptyCommand can_cmd_error_reset;              // 2.2 CMD 12 CAN_CMD_ERROR_RESET
  EmptyCommand can_cmd_get_statusword;           // 2.2 CMD 13 CAN_CMD_GET_STATUSWORD
  EmptyCommand can_cmd_statusword_report;        // 2.2 CMD 14 CAN_CMD_STATUSWORD_REPORT
  EmptyCommand can_cmd_get_value_1;              // 2.2 CMD 15 CAN_CMD_GET_VALUE_1
  EmptyCommand can_cmd_get_value_2;              // 2.2 CMD 16 CAN_CMD_GET_VALUE_2
  SetConfig can_cmd_set_config;                  // 2.2 CMD 17 CAN_CMD_SET_CONFIG
  GetConfig can_cmd_get_config;                  // 2.2 CMD 18 CAN_CMD_GET_CONFIG
  EmptyCommand can_cmd_save_all_config;          // 2.2 CMD 19 CAN_CMD_SAVE_ALL_CONFIG
  EmptyCommand can_cmd_reset_all_config;         // 2.2 CMD 20 CAN_CMD_RESET_ALL_CONFIG
  EmptyCommand can_cmd_sync;                     // 2.2 CMD 21 CAN_CMD_SYNC
  EmptyCommand can_cmd_heartbeat;                // 2.2 CMD 22 CAN_CMD_HEARTBEAT
  StartAuto can_cmd_start_auto;                  // 2.2 CMD 27 CAN_CMD_START_AUTO
  EmptyCommand can_cmd_get_fw_version;           // 2.2 CMD 28 CAN_CMD_GET_FW_VERSION
  EmptyCommand can_cmd_dfu_start;                // 2.2 CMD 29 CAN_CMD_DFU_START
  DfuData can_cmd_dfu_data;                      // 2.2 CMD 30 CAN_CMD_DFU_DATA
  EmptyCommand can_cmd_dfu_end;                  // 2.2 CMD 31 CAN_CMD_DFU_END

  template <typename Archive>
  void Serialize(Archive* a) {
    a->Visit(MJ_NVP(can_cmd_motor_disable));
    a->Visit(MJ_NVP(can_cmd_motor_enable));
    a->Visit(MJ_NVP(can_cmd_set_torque));
    a->Visit(MJ_NVP(can_cmd_set_velocity));
    a->Visit(MJ_NVP(can_cmd_set_position));
    a->Visit(MJ_NVP(can_cmd_calib_start));
    a->Visit(MJ_NVP(can_cmd_calib_report));
    a->Visit(MJ_NVP(can_cmd_calib_abort));
    a->Visit(MJ_NVP(can_cmd_anticogging_start));
    a->Visit(MJ_NVP(can_cmd_anticogging_report));
    a->Visit(MJ_NVP(can_cmd_anticogging_abort));
    a->Visit(MJ_NVP(can_cmd_set_home));
    a->Visit(MJ_NVP(can_cmd_error_reset));
    a->Visit(MJ_NVP(can_cmd_get_statusword));
    a->Visit(MJ_NVP(can_cmd_statusword_report));
    a->Visit(MJ_NVP(can_cmd_get_value_1));
    a->Visit(MJ_NVP(can_cmd_get_value_2));
    a->Visit(MJ_NVP(can_cmd_set_config));
    a->Visit(MJ_NVP(can_cmd_get_config));
    a->Visit(MJ_NVP(can_cmd_save_all_config));
    a->Visit(MJ_NVP(can_cmd_reset_all_config));
    a->Visit(MJ_NVP(can_cmd_sync));
    a->Visit(MJ_NVP(can_cmd_heartbeat));
    a->Visit(MJ_NVP(can_cmd_start_auto));
    a->Visit(MJ_NVP(can_cmd_get_fw_version));
    a->Visit(MJ_NVP(can_cmd_dfu_start));
    a->Visit(MJ_NVP(can_cmd_dfu_data));
    a->Visit(MJ_NVP(can_cmd_dfu_end));
  }
};

}
}
