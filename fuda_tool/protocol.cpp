#include "protocol.h"

namespace
{
CommandDef cmd(quint8 id,
               const QString &enumName,
               const QString &displayName,
               const QString &dlcText,
               PayloadKind payloadKind,
               const QString &parameterHint,
               const QString &description,
               bool txAllowed,
               bool expectsReply,
               bool reportOnly = false)
{
    return CommandDef{id,
                      enumName,
                      displayName,
                      dlcText,
                      payloadKind,
                      parameterHint,
                      description,
                      txAllowed,
                      expectsReply,
                      reportOnly};
}

ConfigDef cfg(quint32 index, ConfigValueType type, const QString &name, const QString &description)
{
    return ConfigDef{index, type, name, description};
}
}

const QVector<CommandDef> &Protocol::commandDefinitions()
{
    static const QVector<CommandDef> commands = {
        cmd(0, QStringLiteral("CAN_CMD_MOTOR_DISABLE"), QStringLiteral("电机使能关闭"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(1, QStringLiteral("CAN_CMD_MOTOR_ENABLE"), QStringLiteral("电机使能"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(2, QStringLiteral("CAN_CMD_SET_TORQUE"), QStringLiteral("设置转矩给定"), QStringLiteral("4"),
            PayloadKind::Float32, QStringLiteral("float: 转矩 Nm"), QStringLiteral("无回复"), true, false),
        cmd(3, QStringLiteral("CAN_CMD_SET_VELOCITY"), QStringLiteral("设置速度给定"), QStringLiteral("4"),
            PayloadKind::Float32, QStringLiteral("float: 速度 turn/s"), QStringLiteral("无回复"), true, false),
        cmd(4, QStringLiteral("CAN_CMD_SET_POSITION"), QStringLiteral("设置位置给定"), QStringLiteral("4"),
            PayloadKind::Float32, QStringLiteral("float: 位置 turn"), QStringLiteral("无回复"), true, false),
        cmd(5, QStringLiteral("CAN_CMD_CALIB_START"), QStringLiteral("启动校准"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(6, QStringLiteral("CAN_CMD_CALIB_REPORT"), QStringLiteral("校准过程上报"), QStringLiteral("8"),
            PayloadKind::None, QStringLiteral("设备主动上报"), QStringLiteral("int32 step + 4 字节 data"), false, false, true),
        cmd(7, QStringLiteral("CAN_CMD_CALIB_ABORT"), QStringLiteral("中止校准"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(8, QStringLiteral("CAN_CMD_ANTICOGGING_START"), QStringLiteral("启动齿槽补偿"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(9, QStringLiteral("CAN_CMD_ANTICOGGING_REPORT"), QStringLiteral("齿槽补偿过程上报"), QStringLiteral("8"),
            PayloadKind::None, QStringLiteral("设备主动上报"), QStringLiteral("int32 step + int32 value"), false, false, true),
        cmd(10, QStringLiteral("CAN_CMD_ANTICOGGING_ABORT"), QStringLiteral("中止齿槽补偿"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(11, QStringLiteral("CAN_CMD_SET_HOME"), QStringLiteral("设当前位置为机械零点"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(12, QStringLiteral("CAN_CMD_ERROR_RESET"), QStringLiteral("清除错误"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(13, QStringLiteral("CAN_CMD_GET_STATUSWORD"), QStringLiteral("查询状态字"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("回复 uint32 status + uint32 errors"), true, true),
        cmd(14, QStringLiteral("CAN_CMD_STATUSWORD_REPORT"), QStringLiteral("状态字上报"), QStringLiteral("8"),
            PayloadKind::None, QStringLiteral("设备主动上报"), QStringLiteral("uint32 status + uint32 errors"), false, false, true),
        cmd(15, QStringLiteral("CAN_CMD_GET_VALUE_1"), QStringLiteral("查询变量 1"), QStringLiteral("18"),
            PayloadKind::OptionalUInt16x9, QStringLiteral("留空；或 9 个 uint16"), QStringLiteral("速度、位置、hall 偏移、hall 值、状态、错误、板载 NTC、Iq 电流、VALUE_1_9"), true, true),
        cmd(16, QStringLiteral("CAN_CMD_GET_VALUE_2"), QStringLiteral("查询变量 2"), QStringLiteral("预留"),
            PayloadKind::HexBytes, QStringLiteral("预留；十六进制字节"), QStringLiteral("预留命令"), true, false),
        cmd(17, QStringLiteral("CAN_CMD_SET_CONFIG"), QStringLiteral("写单个配置项"), QStringLiteral("8"),
            PayloadKind::ConfigWrite, QStringLiteral("index,value"), QStringLiteral("建议使用下方配置表；回复同 GET_CONFIG 格式"), true, true),
        cmd(18, QStringLiteral("CAN_CMD_GET_CONFIG"), QStringLiteral("读单个配置项"), QStringLiteral("4"),
            PayloadKind::ConfigRead, QStringLiteral("index"), QStringLiteral("回复 index + 当前值"), true, true),
        cmd(19, QStringLiteral("CAN_CMD_SAVE_ALL_CONFIG"), QStringLiteral("保存全部配置到 Flash"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(20, QStringLiteral("CAN_CMD_RESET_ALL_CONFIG"), QStringLiteral("恢复出厂配置"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("仅失能下有效；设备回复 int32 返回值，0 成功"), true, true),
        cmd(21, QStringLiteral("CAN_CMD_SYNC"), QStringLiteral("同步触发"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("触发一次目标同步"), true, false),
        cmd(22, QStringLiteral("CAN_CMD_HEARTBEAT"), QStringLiteral("心跳"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备周期发送或主机保活"), true, false),
        cmd(27, QStringLiteral("CAN_CMD_START_AUTO"), QStringLiteral("VALUE_1 自动推送"), QStringLiteral("1"),
            PayloadKind::UInt8, QStringLiteral("uint8: 1 开启，0 关闭"), QStringLiteral("设备回显收到的值；开启后间隔 1 ms 推送"), true, true),
        cmd(28, QStringLiteral("CAN_CMD_GET_FW_VERSION"), QStringLiteral("查询固件版本"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("回复 uint32 主版本 + uint32 次版本"), true, true),
        cmd(29, QStringLiteral("CAN_CMD_DFU_START"), QStringLiteral("DFU 升级开始"), QStringLiteral("0"),
            PayloadKind::None, QStringLiteral("无参数"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(30, QStringLiteral("CAN_CMD_DFU_DATA"), QStringLiteral("DFU 数据包"), QStringLiteral("1~8"),
            PayloadKind::HexBytes, QStringLiteral("1~8 个十六进制字节"), QStringLiteral("设备回复 int32 返回值，0 成功"), true, true),
        cmd(31, QStringLiteral("CAN_CMD_DFU_END"), QStringLiteral("DFU 升级结束"), QStringLiteral("8"),
            PayloadKind::HexBytes, QStringLiteral("8 个十六进制字节"), QStringLiteral("校验通过后设备重启进 Bootloader"), true, true),
    };
    return commands;
}

const QVector<ConfigDef> &Protocol::configDefinitions()
{
    static const QVector<ConfigDef> configs = {
        cfg(1, ConfigValueType::Int32, QStringLiteral("invert_motor_dir"), QStringLiteral("反转电机方向")),
        cfg(2, ConfigValueType::Float32, QStringLiteral("inertia"), QStringLiteral("转动惯量")),
        cfg(3, ConfigValueType::Float32, QStringLiteral("torque_constant"), QStringLiteral("扭矩常数")),
        cfg(4, ConfigValueType::Int32, QStringLiteral("motor_pole_pairs"), QStringLiteral("电机极对数")),
        cfg(5, ConfigValueType::Float32, QStringLiteral("motor_phase_resistance"), QStringLiteral("电机相电阻")),
        cfg(6, ConfigValueType::Float32, QStringLiteral("motor_phase_inductance"), QStringLiteral("电机相电感")),
        cfg(7, ConfigValueType::Float32, QStringLiteral("current_limit"), QStringLiteral("电流限制")),
        cfg(8, ConfigValueType::Float32, QStringLiteral("velocity_limit"), QStringLiteral("速度限制")),
        cfg(9, ConfigValueType::Float32, QStringLiteral("calib_current"), QStringLiteral("校准电流")),
        cfg(10, ConfigValueType::Float32, QStringLiteral("calib_voltage"), QStringLiteral("校准电压")),
        cfg(11, ConfigValueType::Int32, QStringLiteral("control_mode"), QStringLiteral("控制模式")),
        cfg(12, ConfigValueType::Float32, QStringLiteral("pos_gain"), QStringLiteral("位置增益")),
        cfg(13, ConfigValueType::Float32, QStringLiteral("vel_gain"), QStringLiteral("速度增益")),
        cfg(14, ConfigValueType::Float32, QStringLiteral("vel_integrator_gain"), QStringLiteral("速度积分增益")),
        cfg(15, ConfigValueType::Float32, QStringLiteral("current_ctrl_bw"), QStringLiteral("电流控制带宽")),
        cfg(16, ConfigValueType::Int32, QStringLiteral("anticogging_enable"), QStringLiteral("抗齿槽效应使能")),
        cfg(17, ConfigValueType::Int32, QStringLiteral("sync_target_enable"), QStringLiteral("同步目标使能")),
        cfg(18, ConfigValueType::Float32, QStringLiteral("target_velcity_window"), QStringLiteral("目标速度窗口")),
        cfg(19, ConfigValueType::Float32, QStringLiteral("target_position_window"), QStringLiteral("目标位置窗口")),
        cfg(20, ConfigValueType::Float32, QStringLiteral("torque_ramp_rate"), QStringLiteral("扭矩斜坡速率")),
        cfg(21, ConfigValueType::Float32, QStringLiteral("velocity_ramp_rate"), QStringLiteral("速度斜坡速率")),
        cfg(22, ConfigValueType::Float32, QStringLiteral("position_filter_bw"), QStringLiteral("位置滤波器带宽")),
        cfg(23, ConfigValueType::Float32, QStringLiteral("profile_velocity"), QStringLiteral("规划速度")),
        cfg(24, ConfigValueType::Float32, QStringLiteral("profile_accel"), QStringLiteral("规划加速度")),
        cfg(25, ConfigValueType::Float32, QStringLiteral("profile_decel"), QStringLiteral("规划减速度")),
        cfg(26, ConfigValueType::Float32, QStringLiteral("protect_under_voltage"), QStringLiteral("欠压保护值")),
        cfg(27, ConfigValueType::Float32, QStringLiteral("protect_over_voltage"), QStringLiteral("过压保护值")),
        cfg(28, ConfigValueType::Float32, QStringLiteral("protect_over_current"), QStringLiteral("过流保护值")),
        cfg(29, ConfigValueType::Float32, QStringLiteral("protect_i_bus_max"), QStringLiteral("总线最大电流保护")),
        cfg(30, ConfigValueType::Int32, QStringLiteral("node_id"), QStringLiteral("CAN 节点 ID")),
        cfg(31, ConfigValueType::Int32, QStringLiteral("can_baudrate"), QStringLiteral("CAN 波特率")),
        cfg(32, ConfigValueType::Int32, QStringLiteral("heartbeat_consumer_ms"), QStringLiteral("接收心跳超时时间，毫秒")),
        cfg(33, ConfigValueType::Int32, QStringLiteral("heartbeat_producer_ms"), QStringLiteral("发送心跳间隔，毫秒")),
        cfg(34, ConfigValueType::Int32, QStringLiteral("calib_valid"), QStringLiteral("校准有效标志，自动")),
        cfg(35, ConfigValueType::Int32, QStringLiteral("encoder_dir"), QStringLiteral("编码器方向，自动")),
        cfg(36, ConfigValueType::Int32, QStringLiteral("encoder_offset"), QStringLiteral("编码器偏移量，自动")),
        cfg(37, ConfigValueType::Int32, QStringLiteral("offset_lut"), QStringLiteral("偏移查找表，自动")),
    };
    return configs;
}

const CommandDef *Protocol::commandById(quint8 id)
{
    const auto &commands = commandDefinitions();
    for (const CommandDef &command : commands) {
        if (command.id == id) {
            return &command;
        }
    }
    return nullptr;
}

const ConfigDef *Protocol::configByIndex(quint32 index)
{
    const auto &configs = configDefinitions();
    for (const ConfigDef &config : configs) {
        if (config.index == index) {
            return &config;
        }
    }
    return nullptr;
}

QString Protocol::commandName(quint8 id)
{
    if (const CommandDef *command = commandById(id)) {
        return QStringLiteral("%1 %2").arg(command->enumName, command->displayName);
    }
    return QStringLiteral("UNKNOWN_CMD_%1").arg(id);
}

QString Protocol::configTypeName(ConfigValueType type)
{
    switch (type) {
    case ConfigValueType::Int32:
        return QStringLiteral("int32");
    case ConfigValueType::Float32:
        return QStringLiteral("float");
    }
    return QStringLiteral("unknown");
}

QString Protocol::payloadKindHint(PayloadKind kind)
{
    switch (kind) {
    case PayloadKind::None:
        return QStringLiteral("无参数");
    case PayloadKind::Float32:
        return QStringLiteral("float");
    case PayloadKind::UInt8:
        return QStringLiteral("uint8");
    case PayloadKind::HexBytes:
        return QStringLiteral("hex bytes");
    case PayloadKind::ConfigRead:
        return QStringLiteral("index");
    case PayloadKind::ConfigWrite:
        return QStringLiteral("index,value");
    case PayloadKind::OptionalUInt16x9:
        return QStringLiteral("9*uint16 或留空");
    }
    return QStringLiteral("unknown");
}
