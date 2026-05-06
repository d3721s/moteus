#pragma once

#include <cstdint>

#include "mjlib/base/visitor.h"

namespace moteus {
namespace fuda {

struct Requests {
  bool can_cmd_motor_disable = false;        // 2.2 CMD 0 CAN_CMD_MOTOR_DISABLE
  bool can_cmd_motor_enable = false;         // 2.2 CMD 1 CAN_CMD_MOTOR_ENABLE
  float can_cmd_set_torque = 0.0f;           // 2.2 CMD 2 CAN_CMD_SET_TORQUE: float ?? [Nm]
  float can_cmd_set_velocity = 0.0f;         // 2.2 CMD 3 CAN_CMD_SET_VELOCITY: float ?? [turn/s]
  float can_cmd_set_position = 0.0f;         // 2.2 CMD 4 CAN_CMD_SET_POSITION: float ?? [turn]
  bool can_cmd_calib_start = false;          // 2.2 CMD 5 CAN_CMD_CALIB_START
  bool can_cmd_calib_report = false;         // 2.2 CMD 6 CAN_CMD_CALIB_REPORT
  bool can_cmd_calib_abort = false;          // 2.2 CMD 7 CAN_CMD_CALIB_ABORT
  bool can_cmd_anticogging_start = false;    // 2.2 CMD 8 CAN_CMD_ANTICOGGING_START
  bool can_cmd_anticogging_report = false;   // 2.2 CMD 9 CAN_CMD_ANTICOGGING_REPORT
  bool can_cmd_anticogging_abort = false;    // 2.2 CMD 10 CAN_CMD_ANTICOGGING_ABORT
  bool can_cmd_set_home = false;             // 2.2 CMD 11 CAN_CMD_SET_HOME
  bool can_cmd_error_reset = false;          // 2.2 CMD 12 CAN_CMD_ERROR_RESET
  bool can_cmd_get_statusword = false;       // 2.2 CMD 13 CAN_CMD_GET_STATUSWORD
  bool can_cmd_statusword_report = false;    // 2.2 CMD 14 CAN_CMD_STATUSWORD_REPORT
  bool can_cmd_get_value_1 = false;          // 2.2 CMD 15 CAN_CMD_GET_VALUE_1
  bool can_cmd_get_value_2 = false;          // 2.2 CMD 16 CAN_CMD_GET_VALUE_2
  uint64_t can_cmd_set_config = 0;           // 2.2 CMD 17 CAN_CMD_SET_CONFIG: uint32 index + uint32 value
  uint32_t can_cmd_get_config = 0;           // 2.2 CMD 18 CAN_CMD_GET_CONFIG: uint32 index
  bool can_cmd_save_all_config = false;      // 2.2 CMD 19 CAN_CMD_SAVE_ALL_CONFIG
  bool can_cmd_reset_all_config = false;     // 2.2 CMD 20 CAN_CMD_RESET_ALL_CONFIG
  bool can_cmd_sync = false;                 // 2.2 CMD 21 CAN_CMD_SYNC
  bool can_cmd_heartbeat = false;            // 2.2 CMD 22 CAN_CMD_HEARTBEAT
  uint8_t can_cmd_start_auto = 0;            // 2.2 CMD 27 CAN_CMD_START_AUTO: Uint8 enable
  bool can_cmd_get_fw_version = false;       // 2.2 CMD 28 CAN_CMD_GET_FW_VERSION
  bool can_cmd_dfu_start = false;            // 2.2 CMD 29 CAN_CMD_DFU_START
  uint64_t can_cmd_dfu_data = 0;             // 2.2 CMD 30 CAN_CMD_DFU_DATA: 1~8 bytes
  bool can_cmd_dfu_end = false;              // 2.2 CMD 31 CAN_CMD_DFU_END

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
