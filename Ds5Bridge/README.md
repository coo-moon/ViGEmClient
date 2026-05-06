# DS5 Bridge - DualSense 自适应扳机桥接工具

将真实 Sony DualSense (PS5) 手柄的输入转发到 ViGEmBus 创建的虚拟 DualSense 手柄，
并将游戏的输出报告（自适应扳机、震动、LED 灯条）回传给真实手柄。

## 功能

- **输入转发**：真实手柄的按键、摇杆、扳机 → 虚拟 DS5（游戏可见）
- **输出转发**：游戏的自适应扳机、震动、灯条效果 → 真实手柄
- **蓝牙支持**：完整的 BT 输出报告格式（含 CRC32 校验）
- **USB 支持**：标准 USB 输出报告格式

## 使用场景

### Steam 游戏（推荐直接使用）

Steam 原生支持 DualSense 手柄，包括自适应扳机。如果你的游戏通过 Steam 运行，
**不需要本工具**，直接连接手柄即可。

确保 Steam 设置正确：
1. 设置 → 控制器 → 已检测到的控制器 里显示 DualSense
2. 游戏内控制器设置保持 DualSense 模式（不要选 Xbox 布局）

### 非 Steam 游戏（需要本工具）

Epic Games、Ubisoft Connect 等平台可能不原生支持 DualSense。此时使用桥接工具
让游戏通过虚拟手柄识别，同时保留自适应扳机功能。

## 前置要求

1. **ViGEmBus 驱动** — 安装 ViGEmBus 才能创建虚拟手柄
   - 下载：https://github.com/nefarius/ViGEmBus/releases
   - 安装后重启电脑

2. **DualSense 手柄** — 通过 USB 或蓝牙连接到电脑
   - 蓝牙配对：按住 PS + Create 键 3 秒进入配对模式
   - 在 Windows 蓝牙设置中添加 "Wireless Controller"

3. **Visual Studio 2022** — 编译需要（或使用预编译版本）

## 编译

```batch
cd ViGEmClient\Ds5Bridge
build.bat
```

编译成功后生成 `build\ds5_bridge.exe`。

## 运行

```
ds5_bridge.exe              # 自动检测连接方式（USB 或蓝牙）
ds5_bridge.exe --bt         # 强制使用蓝牙模式
```

程序会：
1. 搜索真实 DualSense 手柄
2. 连接 ViGEmBus 驱动
3. 创建虚拟 DualSense 手柄
4. 开始双向转发

按 **Ctrl+C** 停止。

## 自适应扳机测试工具

`test_trigger.exe` 用于直接测试真实手柄的扳机效果（不需要 ViGEmBus）。

```batch
cd ViGEmClient\Ds5Bridge
build_trigger.bat
test_trigger.exe
```

测试内容：
- 连续阻力（Feedback 模式 0x21）
- 扳机震动（Vibration 模式 0x26）
- 手柄震动（Rumble）
- 灯条颜色（Lightbar）

## 技术细节

### 蓝牙输出报告格式

BT 输出报告 ID 0x31，共 78 字节：

```
偏移    大小    字段
[0]     1       Report ID = 0x31
[1]     1       序列号 (SeqNo)
[2]     1       Magic = 0x10（必须设置）
[3]     1       valid_flag0（使能标志低位）
[4]     1       valid_flag1（使能标志高位）
[5]     1       右震动电机（高频/小）
[6]     1       左震动电机（低频/大）
[7..12] 6       音频/麦克风相关
[13..23] 11     右扳机 FFB（模式 + 10字节参数）
[24..34] 11     左扳机 FFB（模式 + 10字节参数）
[35..46] 12     电机功率/LED 控制
[47]    1       灯条 Red
[48]    1       灯条 Green
[49]    1       灯条 Blue
[50..73] 24     填充（全零）
[74..77] 4      CRC32 校验（小端序）
```

### 使能标志 (valid_flag0)

```
bit0 (0x01) = 启用震动
bit1 (0x02) = 使用简单震动（非触觉）
bit2 (0x04) = 启用右扳机 FFB
bit3 (0x08) = 启用左扳机 FFB
```

### 扳机模式值

```
0x05 = 关闭（无效果）
0x21 = Feedback（连续阻力）
0x26 = Vibration（扳机震动）
0x25 = Weapon（模拟扳机枪械手感）
```

### CRC32 计算

使用标准 CRC-32（IEEE 802.3），初始值 0xFFFFFFFF，最终取反。
计算范围为 HIDP 协议头 0xA2 + 报告前 74 字节：

```cpp
uint8_t crcData[75];
crcData[0] = 0xA2;              // HIDP 输出报告头
memcpy(&crcData[1], report, 74); // 报告数据（不含 CRC 本身）
uint32_t crc = standardCRC32(crcData, 75);
report[74..77] = crc (小端序);
```

## 文件说明

```
Ds5Bridge/
├── main.cpp            # 桥接主程序
├── test_trigger.cpp    # 扳机测试工具
├── build.bat           # 编译脚本（桥接程序）
└── build_trigger.bat   # 编译脚本（测试工具）
```

## 已知限制

- 蓝牙模式下手柄休眠后需要按 PS 键重新连接
- 部分游戏可能需要关闭 Steam Input 才能正确识别虚拟手柄
- USB 模式下的输出报告格式未测试
