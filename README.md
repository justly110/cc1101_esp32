# CC1101 多频段遥控信号管理器（ESP32-S3）

> 基于 ESP32-S3 + CC1101 的 OOK/ASK 遥控信号学习、捕获、存储与回放系统。
>
> 当前版本：v0.0.1（对应提交 `ab2cf82`）
>

---

## 1. 项目简介

本项目使用 ESP32-S3 通过 SPI 驱动 CC1101 射频收发芯片，提供一个独立的 Wi-Fi SoftAP 和中文 Web 界面，用于：

- 选择 CC1101 的工作频率（315 / 433 / 443 / 868 / 915 MHz 等常用频点）；
- 以 OOK/ASK 方式扫描遥控器信号；
- 通过 GDO0 边沿中断记录脉冲宽度；
- 对常见遥控编码进行粗略分类（EV1527、PT2262、OOK-PWM）；
- 将捕获结果保存到板载 SPIFFS；
- 通过网页查看、下载、删除已保存信号；
- 将保存的脉冲序列重新发射，实现遥控信号回放；
- 通过 HTTP API 进行同样的操作。

系统默认工作在 **315 MHz**，方便直接使用常见的 315 MHz 遥控器测试。模块标称 443 MHz 时，也可以在网页中选择 443 MHz；实际使用时应选择与遥控器、天线匹配的频率。

---

## 2. 功能特性

### 2.1 网络与界面

- ESP32-S3 工作在 **SoftAP 模式**（不连接路由器）；
- 默认 AP IP：`192.168.4.1`；
- 内置中文 Web UI，无需安装手机 App；
- 提供 REST 风格 HTTP API。

### 2.2 射频功能

- CC1101 SPI 驱动，SPI2_HOST，4 MHz，SPI 模式 0；
- 支持 CC1101 的三个硬件频段：
  - 300–348 MHz（常用 315、330 MHz）；
  - 387–464 MHz（常用 390、418、430、433.42、433.92、443、447 MHz）；
  - 779–928 MHz（常用 868.35、915 MHz）。
- 当前固件实际使用 **OOK/ASK 异步串行模式**，通过 GDO0 输出解调后的脉冲；
- 支持频率切换，切换时会重新写入 FREQ2/FREQ1/FREQ0 并重新校准；
- 扫描时使用 RSSI 判断载波，阈值约为 `-75 dBm`；
- 单次最多记录 512 个脉冲宽度；
- 检测到载波后开始记录，静默约 150 ms 后结束一次捕获；
- 对捕获结果进行启发式分类：
  - `EV1527 (24bit)`
  - `PT2262 (12bit)`
  - `OOK-PWM`
  - `Unknown`

### 2.3 存储与回放

- 使用板载 SPIFFS 保存信号，分区大小 1 MB；
- 信号文件名按“频率标签_序号”生成，例如 `315_1`、`43392_1`、`86835_1`；
- 支持列表、下载、删除；
- 扫描保存的是脉冲文件，可直接用于发射；
- 支持通过网页上传任意文件并保存/下载，但只有符合脉冲文件格式的文件才能发射。

### 2.4 调试

- 串口日志输出 SPIFFS 挂载、AP IP、CC1101 检测结果、扫描/检测/发射过程；
- Web 状态页显示 CC1101 是否连接、当前频率、系统状态；
- 网页会保存每个信号的发射成功次数（浏览器 `localStorage`）。

---

## 3. 硬件要求与接线

### 3.1 硬件清单

- ESP32-S3 开发板（建议 16 MB Flash，至少 4 MB）；
- CC1101 模块（315 / 433 / 443 / 868 / 915 MHz 版本按需选择）；
- 2.4 GHz Wi-Fi 天线由 ESP32-S3 板载提供；
- CC1101 天线（必须与工作频率匹配）；
- 3.3 V 电源、杜邦线、必要时的电平/电源滤波。

> CC1101 只能使用 3.3 V，不要接 5 V。

### 3.2 接线表（以当前代码为准）

| ESP32-S3 | CC1101 | 说明 |
|---|---|---|
| `3.3V` | `VCC` | 3.3 V 供电 |
| `GND` | `GND` | 必须共地 |
| `GPIO10` | `CS` | SPI 片选 |
| `GPIO12` | `SCLK` | SPI 时钟 |
| `GPIO11` | `MOSI` | SPI 数据输出 |
| `GPIO13` | `MISO` | SPI 数据输入 |
| `GPIO9` | `GDO0` | OOK 解调数据输出 / 发射时作为输出脚 |
| 不接 | `GDO2` | 当前固件未使用 |

注意：

- **GDO0 必须连接**。扫描时它作为边沿中断输入，发射时它被切换为输出，用于发送 OOK 脉冲。
- `GDO2` 在代码中只配置了 `IOCFG2`，但没有分配 ESP32 引脚，可以不接。
- 旧版文档中的 `GPIO2/GPIO18/GPIO19/GPIO21` 接线已经不再适用，请以本表为准。
- 仓库中的 `接线图.png` 可作参考；如果图片与本表不一致，以本表和 `main/cc1101_driver.h` 中的定义为准。

### 3.3 天线与频率

- 315 MHz 遥控器请使用 315 MHz 天线或至少长度接近的导线；
- 433.92 / 443 MHz 请使用对应频段天线；
- 868 / 915 MHz 必须使用对应频段天线；
- 天线不匹配时，接收距离会明显缩短，甚至完全收不到信号；
- 测试时先把遥控器靠近模块（几十厘米内），确认能检测到后再逐步拉开距离。

---

## 4. 软件环境

- **ESP-IDF 5.5.2**（见 `dependencies.lock`）；
- 目标芯片：`esp32s3`；
- Flash：当前 `sdkconfig` 配置为 16 MB；
- 分区表：`partitions.csv`（自定义分区，包含 1 MB SPIFFS）；
- Python 3、CMake、Ninja 等由 ESP-IDF 安装工具提供。

Windows 下建议使用 ESP-IDF PowerShell 或 ESP-IDF Terminal，确保 `idf.py` 在 PATH 中。

---

## 5. 目录结构

```text
cc1101_esp32/
├── CMakeLists.txt                  # 工程入口
├── partitions.csv                  # 自定义分区表（含 storage/SPIFFS）
├── sdkconfig.defaults              # 默认配置（自定义分区表）
├── sdkconfig                       # 当前构建配置（本地生成）
├── main/
│   ├── main.c                      # app_main：NVS、SPIFFS、Wi-Fi AP、Web、CC1101 初始化
│   ├── cc1101_driver.c             # SPI、频率、OOK 扫描、原始采集、脉冲发射
│   ├── cc1101_driver.h             # 引脚、寄存器、数据结构、API 声明
│   ├── web_server.c                # HTTP 服务、Web UI、REST API、文件管理
│   ├── web_server.h                # Web 服务 API 声明
│   ├── Kconfig.projbuild           # menuconfig 中的 SoftAP 配置
│   └── CMakeLists.txt              # main 组件依赖
├── 接线图.png                      # 接线参考图
├── README.md                       # 本文档
├── PROJECT_SUMMARY.md              # 项目概要（部分内容可能滞后）
├── demo_cc1101_signal_manager.py   # PC 端模拟演示，不连接硬件
├── setup_and_test.py               # 早期辅助脚本
└── validate_project.py             # 早期结构检查脚本
```

---

## 6. 编译与烧录

### 6.1 设置目标

```bash
cd D:\code\cc1101_esp32
idf.py set-target esp32s3
```

### 6.2 配置 Wi-Fi AP（可选）

```bash
idf.py menuconfig
```

进入：

```text
Example Configuration
  └── SoftAP Configuration
        ├── WiFi AP SSID
        ├── WiFi AP Password
        ├── WiFi AP Channel
        └── Maximal STA connections
```

默认值：

| 配置项 | 默认值 |
|---|---|
| SSID | `myssid` |
| 密码 | `mypassword` |
| 信道 | `1` |
| 最大连接数 | `4` |

> 生产或长期使用时请修改默认密码。
>
> `Kconfig.projbuild` 中还有一组 STA 配置项，但当前固件只使用 AP 模式，这些 STA 配置不会生效。

### 6.3 编译

```bash
idf.py build
```

### 6.4 烧录与串口监视

```bash
idf.py -p COMx flash monitor
```

将 `COMx` 替换为实际串口，例如 `COM5`。

如果之前烧录过旧版本（单 app 分区），建议先擦除整片 Flash，确保新的分区表生效：

```bash
idf.py -p COMx erase-flash
idf.py -p COMx flash monitor
```

> `erase-flash` 会清除 NVS 和 SPIFFS 中已保存的信号。

### 6.5 分区说明

`partitions.csv` 当前内容：

| 分区 | 类型 | 偏移 | 大小 | 用途 |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | `0x6000` | Wi-Fi/NVS |
| `phy_init` | data/phy | `0xf000` | `0x1000` | 射频校准数据 |
| `factory` | app/factory | `0x10000` | `0x200000` | 应用程序（2 MB） |
| `storage` | data/spiffs | `0x210000` | `0x100000` | 信号存储（1 MB） |

当前固件约 0.9 MB，2 MB 的 app 分区足够。如果后续功能明显增加，需要同步调整 `factory` 大小和 `storage` 偏移。

---

## 7. 首次运行

### 7.1 上电与连接

1. 给 ESP32-S3 上电；
2. 在电脑或手机上搜索 Wi-Fi；
3. 连接默认 AP：
   - SSID：`myssid`
   - 密码：`mypassword`
4. 浏览器打开：

```text
http://192.168.4.1/
```

如果修改过 `menuconfig`，请使用修改后的 SSID/密码。

### 7.2 启动日志示例

串口正常启动时，大致会看到：

```text
SPIFFS mounted: total=1048576, used=...
WiFi SoftAP: wifi_init_softap finished. SSID:myssid password:mypassword channel:1
WiFi SoftAP: AP IP: 192.168.4.1
WEB_SERVER: HTTPD started successfully
CC1101: CC1101 detected: PARTNUM=0x00 VERSION=0x04
CC1101: CC1101 configuration written successfully (FREQ=0x0C1D89)
WiFi SoftAP: Web interface available at: http://192.168.4.1
```

如果看到：

```text
CC1101: CC1101 NOT detected: ...
```

请优先检查接线、供电和 CS/SCLK 是否接反。

> `cc1101_init()` 在未检测到芯片时仍会继续运行，Web 服务也会启动；此时状态页会显示“未连接”，扫描/采集接口会返回错误。

---

## 8. Web 界面使用

打开 `http://192.168.4.1/` 后，页面包含以下区域。

### 8.1 系统状态

- 系统状态：运行中；
- CC1101：已连接 / 未连接；
- 无线模式：SoftAP，地址 `http://192.168.4.1`；
- 当前工作频率；
- 接口状态 JSON 摘要。

### 8.2 工作频率

下拉框内置常用频点：

- 315.00 MHz
- 330.00 MHz
- 390.00 MHz
- 418.00 MHz
- 430.00 MHz
- 433.42 MHz
- 433.92 MHz
- 447.00 MHz
- 868.35 MHz
- 915.00 MHz

另外可以选择“自定义...”，输入 300–928 MHz 之间的频率。

注意：

- 驱动只接受 CC1101 的三个硬件频段：`300–348`、`387–464`、`779–928 MHz`；
- 443 MHz 可以通过“自定义”输入 `443` 后应用；
- 默认频率是 **315 MHz**，不是 443 MHz；
- 切换频率会自动停止正在进行的扫描；
- 如果频率与遥控器/天线不匹配，可能完全收不到信号。

### 8.3 开始捕获（扫描模式）

1. 选择正确的工作频率；
2. 点击“开始捕获”；
3. 页面显示“正在 xxx MHz 扫描，请按下遥控器...”；
4. 在模块附近按下遥控器按键；
5. 检测到信号后，页面显示：
   - 分类结果（EV1527 / PT2262 / OOK-PWM / Unknown）；
   - RSSI（dBm）；
   - 脉冲数量；
   - 总时长（µs）；
   - 是否已保存及文件名；
6. 扫描自动停止，信号自动保存到 SPIFFS。

扫描规则：

- RSSI 高于约 `-75 dBm` 时认为出现载波；
- 至少记录到 24 个边沿才可能判定为有效信号；
- 单次最多记录 512 个脉冲；
- 载波消失、静默约 150 ms 后结束本次捕获；
- 只保存第一次有效检测，随后自动停止。

### 8.4 已保存信号

表格列：

- `ID`：显示用序号，每次刷新可能变化，**不要作为稳定标识**；
- `文件名`：真实存储键，例如 `315_1`；
- `大小`：字节数；
- `发射`：回放该信号；
- `查看`：以十六进制显示前 512 字节；
- `删除`：删除文件；
- `发射结果`：最近一次发射状态；
- `成功次数`：浏览器本地记录的发射成功次数。

### 8.5 上传信号文件

点击“上传信号文件”，选择本地文件：

- 浏览器会读取文件并转换为 Base64；
- 通过 `POST /api/signal/capture` 上传；
- 文件按当前频率标签和下一个序号保存；
- 上传的文件可以在列表中查看、下载、删除。

注意：

- 上传的任意文件 **不一定是可发射的脉冲文件**；
- 只有符合 `C110` 脉冲格式的文件才能发射；
- 上传大文件会占用较多内存，建议控制在几百 KB 以内；
- SPIFFS 总空间只有 1 MB，配置文件上限虽然为 1 MB，但实际可用空间更小。

### 8.6 查看与下载

- “查看”会请求 `/api/signal/download/<文件名>`，并在页面显示前 512 字节的十六进制；
- 直接访问下载地址可以保存原始文件：

```text
http://192.168.4.1/api/signal/download/315_1
```

### 8.7 发射信号

1. 确认当前频率与信号记录时一致；
2. 在“已保存信号”中点击“发射”；
3. 页面显示“发射中...”，完成后显示“发射成功”或“发射失败”。

发射行为：

- 只接受扫描生成的 `C110` 脉冲文件；
- 发射时 GDO0 被切换为输出，按脉冲宽度交替输出高/低电平；
- 发射是阻塞操作，期间不要同时进行扫描；
- 发射功率由 PA 表配置，代码写入约 `+10 dBm`，实际距离取决于模块、天线和供电；
- 发射时序由 GPIO 软件延时生成，不同遥控协议的兼容性有限。

### 8.8 停止

- “停止捕获”会停止扫描；
- 也可以通过 `POST /api/signal/stop` 停止原始采集。

---

## 9. 工作原理

### 9.1 频率配置

驱动使用 26 MHz 晶振计算频率字：

```text
FREQ = f_carrier / 26 MHz × 2^16
```

分别写入 `FREQ2`、`FREQ1`、`FREQ0`。写入前会先进入 IDLE，写入后执行 `SCAL` 校准。

默认频率：

```c
#define CC1101_DEFAULT_FREQUENCY 315000000U
```

### 9.2 OOK 扫描

扫描流程：

1. 进入 IDLE；
2. 重新写入当前频率；
3. 执行 `SCAL`；
4. 配置为 OOK/ASK 异步串行模式；
5. GDO0 配置为串行数据输出；
6. 进入 RX；
7. 后台任务每约 20 ms 读取一次 RSSI；
8. RSSI 超过阈值后，使能 GDO0 双边沿中断；
9. 中断服务程序记录每个边沿的时间戳；
10. 静默约 150 ms 后结束记录；
11. 计算相邻边沿的时间差，得到脉冲宽度（µs）；
12. 进行分类并保存结果。

扫描参数（`main/cc1101_driver.h`）：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `CC1101_MAX_PULSES` | 512 | 单次最多脉冲数 |
| `CC1101_RSSI_THRESHOLD` | -75 dBm | 载波检测阈值 |
| `CC1101_SIGNAL_BUF_SIZE` | 8192 | 原始采集缓冲区 |
| `CC1101_SIGNAL_SAMPLE_RATE` | 38400 | 兼容旧代码的常量，当前不参与采样控制 |

### 9.3 分类规则

分类是启发式的，不是严格协议解码：

- 脉冲数少于 8：`Unknown`；
- 存在明显长同步脉冲且脉冲数 ≥ 40：`EV1527 (24bit)`；
- 存在明显长同步脉冲且脉冲数 ≥ 24：`PT2262 (12bit)`；
- 脉冲数 ≥ 16：`OOK-PWM`；
- 其他：`Unknown`。

因此：

- 分类结果仅供快速判断；
- `Unknown` 不代表信号不可用，仍然可以保存和回放；
- 不同遥控器的脉宽比例可能不同，必要时需要调整分类阈值。

### 9.4 原始 FIFO 采集

除扫描模式外，驱动还提供一次性的原始 FIFO 采集：

- 分配 8192 字节缓冲区；
- 启动采集任务；
- 每 5 ms 读取 `RXBYTES`；
- 每次最多从 FIFO 读取 64 字节；
- 3 秒超时或缓冲区满后停止；
- 结果通过 `cc1101_capture_get_data()` 获取。

对应的 HTTP 接口是：

```text
POST /api/signal/start
POST /api/signal/stop
```

该模式保存的是原始字节流，**不是脉冲文件**，不能直接用于发射。Web UI 的“开始捕获”使用的是扫描模式，不是这个接口。

### 9.5 发射

`cc1101_transmit_pulses()` 的流程：

1. 检查脉冲指针、数量（1–512）和驱动状态；
2. 进入 IDLE，重新应用当前频率并校准；
3. 配置 OOK 发射相关寄存器；
4. 清空 TX FIFO；
5. 将 GDO0 切换为输出；
6. 发送 `STX`；
7. 按 `pulses[]` 中的微秒宽度交替输出高/低电平；
8. 发送 `SIDLE`；
9. 将 GDO0 恢复为输入，恢复 `IOCFG0`；
10. 返回结果。

> 发射是软件延时生成的，时序精度受 FreeRTOS 调度和中断影响。对时序要求严格的协议，建议后续改用 RMT 或硬件定时器。

### 9.6 存储

- 文件系统：SPIFFS；
- 挂载点：`/spiffs`；
- 分区标签：`storage`；
- 分区大小：1 MB；
- 最大同时打开文件数：5；
- 挂载失败时自动格式化（`format_if_mount_failed = true`）。

---

## 10. HTTP API

所有接口默认端口为 `80`，基础地址：

```text
http://192.168.4.1
```

### 10.1 接口一览

| 方法 | 路径 | 参数 | 说明 |
|---|---|---|---|
| GET | `/` | 无 | 返回中文 Web UI |
| GET | `/api/status` | 无 | 系统状态 |
| GET | `/api/frequency` | 无 | 当前频率和预设频点 |
| POST | `/api/frequency` | `hz` 或 `mhz` | 设置频率 |
| POST | `/api/signal/scan/start` | 无 | 开始 OOK 扫描 |
| POST | `/api/signal/scan/stop` | 无 | 停止扫描 |
| GET | `/api/signal/scan/status` | 无 | 查询扫描状态/检测结果 |
| POST | `/api/signal/start` | 无 | 一次性原始 FIFO 采集（阻塞约 4 秒） |
| POST | `/api/signal/stop` | 无 | 停止原始采集 |
| POST | `/api/signal/transmit` | `file` | 发射脉冲文件 |
| GET | `/api/signal/capture` | 无 | 返回采集服务就绪状态 |
| POST | `/api/signal/capture` | JSON body | 上传 Base64 文件并保存 |
| DELETE | `/api/signal/capture` | `file` | 删除文件 |
| GET | `/api/signal/list` | 无 | 已保存信号列表 |
| GET | `/api/signal/download/<文件名>` | 路径参数 | 下载文件 |

> 业务错误通常仍返回 HTTP 200，需要在 JSON 的 `status` 字段中判断成功或失败。

### 10.2 系统状态

```bash
curl http://192.168.4.1/api/status
```

示例：

```json
{
  "status": "running",
  "timestamp": 1701234567,
  "wifi_status": "connected",
  "cc1101_status": "connected",
  "web_server_status": "active",
  "frequency": 315000000,
  "frequency_mhz": 315
}
```

说明：

- `wifi_status` 当前固定为 `connected`，表示 AP 已启动；
- `cc1101_status` 为 `connected` 或 `disconnected`；
- `frequency` 单位 Hz。

### 10.3 查询频率

```bash
curl http://192.168.4.1/api/frequency
```

示例：

```json
{
  "frequency": 315000000,
  "frequency_mhz": 315,
  "cc1101_initialized": true,
  "presets": [
    { "hz": 315000000, "mhz": 315, "label": "315.00 MHz 常见门禁/遥控" },
    { "hz": 433920000, "mhz": 433.92, "label": "433.92 MHz 最常见" }
  ]
}
```

### 10.4 设置频率

使用 Hz：

```bash
curl -X POST "http://192.168.4.1/api/frequency?hz=433920000"
```

或使用 MHz：

```bash
curl -X POST "http://192.168.4.1/api/frequency?mhz=433.92"
```

成功：

```json
{
  "status": "ok",
  "message": "频率已切换",
  "frequency": 433920000,
  "frequency_mhz": 433.92
}
```

失败：

```json
{
  "status": "error",
  "message": "CC1101 不支持该频率（仅 300-348 / 387-464 / 779-928 MHz）"
}
```

说明：

- 如果正在扫描，会先停止扫描；
- 设置前会检查 100 MHz–1 GHz，随后由驱动检查 CC1101 频段；
- 频率切换会重新写频率寄存器并校准。

### 10.5 开始扫描

```bash
curl -X POST http://192.168.4.1/api/signal/scan/start
```

成功：

```json
{
  "status": "scanning",
  "message": "扫描中，请按下遥控器"
}
```

失败：

```json
{
  "status": "error",
  "message": "CC1101 未连接，请检查接线"
}
```

### 10.6 查询扫描状态

```bash
curl http://192.168.4.1/api/signal/scan/status
```

扫描中：

```json
{
  "state": "scanning",
  "frequency": 315000000,
  "frequency_mhz": 315
}
```

检测到信号：

```json
{
  "state": "detected",
  "format": "EV1527 (24bit)",
  "rssi": -62,
  "pulses": 48,
  "duration_us": 12345,
  "saved": true,
  "filename": "315_1",
  "frequency": 315000000,
  "frequency_mhz": 315
}
```

空闲：

```json
{
  "state": "idle",
  "frequency": 315000000,
  "frequency_mhz": 315
}
```

说明：

- 检测结果在第一次轮询到 `detected` 状态时保存；
- 如果保存失败，`saved` 为 `false`，下次轮询会重试；
- API 使用者应持续轮询该接口，直到状态变为 `detected` 或 `idle`。

### 10.7 停止扫描

```bash
curl -X POST http://192.168.4.1/api/signal/scan/stop
```

响应：

```json
{
  "status": "idle",
  "message": "已停止扫描"
}
```

### 10.8 一次性原始采集

开始：

```bash
curl -X POST http://192.168.4.1/api/signal/start
```

该请求会等待采集结束（最多约 4 秒），然后返回：

```json
{
  "status": "success",
  "message": "捕获成功",
  "filename": "315_2",
  "size": 8192
}
```

未捕获到数据：

```json
{
  "status": "empty",
  "message": "未捕获到信号，请靠近遥控器再试",
  "size": 0
}
```

停止：

```bash
curl -X POST http://192.168.4.1/api/signal/stop
```

> 该接口保存的是原始 FIFO 字节流，不是脉冲文件，不能直接通过 `/api/signal/transmit` 发射。

### 10.9 发射信号

```bash
curl -X POST "http://192.168.4.1/api/signal/transmit?file=315_1"
```

成功：

```json
{
  "status": "success",
  "message": "已发射信号",
  "pulses": 48,
  "filename": "315_1",
  "frequency": 315000000,
  "frequency_mhz": 315
}
```

失败：

```json
{
  "status": "error",
  "message": "信号文件无效或不存在"
}
```

说明：

- `file` 必须是 `/spiffs` 中的脉冲文件；
- 文件名必须安全（不能包含 `/`、`\`、`..`）；
- 发射期间不要同时扫描；
- 文件格式见第 11 节。

### 10.10 上传文件

```bash
curl -X POST http://192.168.4.1/api/signal/capture \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"upload\",\"data\":\"$(base64 -w0 signal.bin)\"}"
```

Windows PowerShell：

```powershell
$b64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes("signal.bin"))
$body = @{ type = "upload"; data = $b64 } | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "http://192.168.4.1/api/signal/capture" `
  -ContentType "application/json" -Body $body
```

成功：

```json
{
  "status": "success",
  "message": "Signal captured and saved successfully",
  "filename": "315_2",
  "size": 1036,
  "timestamp": 1701234567
}
```

说明：

- `data` 必须是 Base64 字符串；
- `type` 字段当前仅用于前端标识，服务端不会根据它改变保存逻辑；
- 上传文件按当前频率标签和下一个序号保存；
- 上传的文件不保证可发射。

### 10.11 删除文件

```bash
curl -X DELETE "http://192.168.4.1/api/signal/capture?file=315_1"
```

响应：

```json
{
  "status": "success",
  "message": "Signal deleted successfully",
  "filename": "315_1"
}
```

### 10.12 信号列表

```bash
curl http://192.168.4.1/api/signal/list
```

示例：

```json
[
  {
    "id": 1,
    "filename": "315_1",
    "size": 1036,
    "modified_time": 1701234567
  },
  {
    "id": 2,
    "filename": "43392_1",
    "size": 1036,
    "modified_time": 1701234600
  }
]
```

说明：

- `id` 是每次请求重新编号的显示序号，不是稳定主键；
- 请使用 `filename` 进行下载、删除和发射。

### 10.13 下载文件

```bash
curl -O "http://192.168.4.1/api/signal/download/315_1"
```

浏览器直接访问：

```text
http://192.168.4.1/api/signal/download/315_1
```

响应头包含：

```text
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="315_1"
```

---

## 11. 信号文件格式

### 11.1 脉冲文件（可发射）

扫描检测成功后保存的是 `pulse_file_t` 结构，小端序、紧凑排列：

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `uint32_t` | `magic` | `0x43313130` |
| 4 | `uint16_t` | `version` | 当前为 `1` |
| 6 | `uint16_t` | `pulse_count` | 脉冲数量，≤ 512 |
| 8 | `int16_t` | `rssi` | 峰值 RSSI，单位 dBm |
| 10 | `uint8_t` | `format` | 分类枚举 |
| 11 | `uint8_t` | `reserved` | 保留 |
| 12 | `uint16_t[512]` | `pulses` | 脉冲宽度，单位 µs，高低交替 |

总大小：

```text
4 + 2 + 2 + 2 + 1 + 1 + 512 × 2 = 1036 字节
```

分类枚举：

| 值 | 名称 |
|---|---|
| 0 | Unknown |
| 1 | OOK-PWM |
| 2 | EV1527 |
| 3 | PT2262 |

发射时只使用 `pulse_count` 和 `pulses[]`，`rssi`、`format` 用于显示。

### 11.2 原始文件（不可直接发射）

以下来源保存的是原始字节：

- `POST /api/signal/start` 的一次性 FIFO 采集；
- `POST /api/signal/capture` 上传的任意文件。

这些文件没有 `C110` 头，`/api/signal/transmit` 会返回“信号文件无效或不存在”。它们仍可列表、下载、删除。

### 11.3 文件命名

文件名格式：

```text
<频率标签>_<序号>
```

频率标签由当前频率（kHz）去掉末尾的 0 得到：

| 频率 | 标签 | 示例文件名 |
|---|---|---|
| 315.00 MHz | `315` | `315_1` |
| 330.00 MHz | `330` | `330_1` |
| 390.00 MHz | `390` | `390_1` |
| 418.00 MHz | `418` | `418_1` |
| 430.00 MHz | `430` | `430_1` |
| 433.42 MHz | `43342` | `43342_1` |
| 433.92 MHz | `43392` | `43392_1` |
| 443.00 MHz | `443` | `443_1` |
| 447.00 MHz | `447` | `447_1` |
| 868.35 MHz | `86835` | `86835_1` |
| 915.00 MHz | `915` | `915_1` |

同一频率下，序号从已有最大值 + 1 开始。

---

## 12. 配置说明

### 12.1 Wi-Fi AP

通过 `idf.py menuconfig` 修改：

```text
Example Configuration → SoftAP Configuration
```

可配置：

- SSID；
- 密码（空密码时使用开放网络）；
- 信道（1–14）；
- 最大 STA 连接数。

### 12.2 频率

频率不在 Kconfig 中配置，通过 Web UI 或 `/api/frequency` 设置。

默认值在 `main/cc1101_driver.h`：

```c
#define CC1101_DEFAULT_FREQUENCY 315000000U
```

### 12.3 引脚

引脚定义在 `main/cc1101_driver.h`：

```c
#define CC1101_SPI_HOST   SPI2_HOST
#define CC1101_IRQ_GPIO   GPIO_NUM_9
#define CC1101_CS_GPIO    GPIO_NUM_10
#define CC1101_SCLK_GPIO  GPIO_NUM_12
#define CC1101_MOSI_GPIO  GPIO_NUM_11
#define CC1101_MISO_GPIO  GPIO_NUM_13
```

修改引脚后需要重新编译并烧录。

### 12.4 扫描参数

在 `main/cc1101_driver.h` 中调整：

```c
#define CC1101_MAX_PULSES       512
#define CC1101_RSSI_THRESHOLD   (-75)
#define CC1101_SIGNAL_BUF_SIZE  8192
```

- `CC1101_MAX_PULSES`：单次最多脉冲数，增大后内存占用增加；
- `CC1101_RSSI_THRESHOLD`：载波检测阈值，数值越接近 0 越难触发；
- `CC1101_SIGNAL_BUF_SIZE`：原始采集缓冲区大小。

### 12.5 分区表

`partitions.csv` 已包含 `storage` SPIFFS 分区。`sdkconfig.defaults` 中已设置：

```text
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

如果修改分区表，建议执行一次 `idf.py erase-flash` 后再烧录。

---

## 13. 串口日志

常见日志及含义：

```text
WEB_SERVER: SPIFFS mounted: total=1048576, used=0
```

SPIFFS 挂载成功。

```text
CC1101: CC1101 detected: PARTNUM=0x00 VERSION=0x04
```

检测到 CC1101，SPI 通信正常。

```text
CC1101: CC1101 NOT detected: PARTNUM=0xFF VERSION=0xFF (check wiring)
```

未检测到芯片，检查接线、供电、CS/SCLK/MISO/MOSI。

```text
CC1101: Carrier detected (-60 dBm), recording
```

检测到载波并开始记录。

```text
CC1101: Detected: EV1527 (24bit), 48 pulses, 12345 us, RSSI -60 dBm
```

捕获并分类成功。

```text
CC1101: Transmitted 48 pulses
```

发射完成。

---

## 14. 故障排查

### 14.1 搜不到 Wi-Fi

- 确认 ESP32-S3 已正常供电并启动；
- 查看串口是否有 AP 启动日志；
- 默认 SSID 是 `myssid`，密码是 `mypassword`；
- 确认手机/电脑支持 2.4 GHz Wi-Fi；
- 如果修改过 `menuconfig`，使用修改后的配置；
- 必要时执行 `idf.py -p COMx erase-flash` 后重新烧录。

### 14.2 网页打不开

- 确认已连接 AP；
- 使用 `http://192.168.4.1`，不要使用 `https`；
- 查看串口是否有 `HTTPD started successfully`；
- 如果 AP 已连接但网页无响应，尝试重新上电。

### 14.3 CC1101 未连接

按顺序检查：

1. `VCC` 是否为 3.3 V；
2. `GND` 是否共地；
3. `CS` 是否接 GPIO10；
4. `SCLK` 是否接 GPIO12；
5. `MOSI` 是否接 GPIO11；
6. `MISO` 是否接 GPIO13；
7. `GDO0` 是否接 GPIO9（扫描/发射必需）；
8. 杜邦线是否接触良好；
9. 模块是否损坏或供电不足。

串口应显示：

```text
PARTNUM=0x00 VERSION=0x04
```

或 `VERSION=0x14`。

### 14.4 扫描不到信号

- 确认工作频率与遥控器一致（默认 315 MHz）；
- 433.92 MHz 遥控器请选择 433.92 MHz；
- 443 MHz 模块请选择 443 MHz 并确认天线匹配；
- 让遥控器靠近模块（先 10–50 cm）；
- 确认 GDO0 已连接；
- 查看串口是否出现 `Carrier detected`；
- 如果 RSSI 一直很低，检查天线和供电；
- 更换遥控器电池。

### 14.5 检测到信号但分类为 Unknown

- 分类是启发式的，`Unknown` 仍然可以保存和回放；
- 可以尝试多次按键，选择脉冲数量较完整的一次；
- 不同遥控器的同步脉冲比例可能不同；
- 必要时调整 `cc1101_classify_pulses()` 中的阈值。

### 14.6 发射失败

- 确认文件是扫描生成的脉冲文件（`C110` 头）；
- 上传的任意文件、`/api/signal/start` 保存的原始文件不能直接发射；
- 确认当前频率与记录时一致；
- 确认没有正在进行的扫描；
- 查看串口是否输出 `Transmitted N pulses`；
- 发射距离短时，检查天线、供电和 PA 配置。

### 14.7 SPIFFS 挂载失败

串口出现：

```text
Failed to mount SPIFFS
```

处理：

1. 确认 `partitions.csv` 中有 `storage` 分区；
2. 确认 `sdkconfig` 使用自定义分区表；
3. 执行 `idf.py erase-flash` 后重新烧录；
4. 确认 Flash 大小与 `sdkconfig` 一致。

### 14.8 上传失败或网页卡顿

- 文件过大，建议控制在几百 KB；
- SPIFFS 总空间只有 1 MB；
- 上传时浏览器需要先做 Base64 编码，大文件会占用较多内存；
- 可以先用小文件验证流程。

### 14.9 状态页显示 CC1101 未连接

- 状态接口每次都会读取 PARTNUM/VERSION；
- 如果 SPI 通信失败，会显示未连接；
- 检查接线和供电；
- 修复后重新触发扫描或设置频率，代码会尝试重新初始化。

### 14.10 重新烧录后信号丢失

- `idf.py erase-flash` 会清除 SPIFFS；
- 修改分区表后重新烧录也可能需要擦除；
- 普通 `idf.py flash` 不会主动清空 SPIFFS。

---

## 15. 已知限制

1. **仅 SoftAP 模式**：当前固件不连接外部路由器，也没有 STA 联网功能。
2. **无鉴权/无 HTTPS**：Web UI 和 API 没有登录认证，请只在可信网络中使用。
3. **OOK/ASK 专用**：当前实现面向 OOK/ASK 遥控信号；`CC1101_MODULATION_GFSK` 常量仅保留兼容，实际配置为 OOK/ASK 异步串行模式。
4. **分类为启发式**：EV1527/PT2262 只是根据脉冲数量和大致的同步脉冲比例判断，不是严格协议解码。
5. **单次单信号**：扫描检测到第一个有效信号后自动停止，不会连续保存多个按键。
6. **脉冲数量上限**：单次最多 512 个脉冲，超长协议可能被截断。
7. **RSSI 阈值固定**：默认 -75 dBm，编译期常量，暂不支持网页调整。
8. **原始采集不可发射**：`/api/signal/start` 和上传的任意文件保存的是原始字节，不能直接回放。
9. **发射为软件时序**：GDO0 由 GPIO 软件翻转，时序精度受调度影响，不保证兼容所有遥控协议。
10. **存储空间有限**：SPIFFS 只有 1 MB，配置文件上限 1 MB 是代码上限，实际可用空间更小。
11. **默认密码较弱**：`myssid` / `mypassword` 仅用于演示，请通过 `menuconfig` 修改。
12. **频率与天线必须匹配**：模块标称 443 MHz 不代表在 315/868/915 MHz 上有最佳性能。
13. **寄存器表面向 OOK 模式**：如需扩展 GFSK、包模式、FEC 等功能，请先对照 CC1101 数据手册核对 `cc1101_driver.h` 中 `0x16` 之后的 MCSM/FREND/FSCAL 等寄存器地址。
14. **旧接口保留**：`cc1101_send_signal()` 仍是早期占位实现，Web UI 发射走的是 `cc1101_transmit_pulses()`。

---

## 16. 开发说明

### 16.1 修改默认频率

编辑 `main/cc1101_driver.h`：

```c
#define CC1101_DEFAULT_FREQUENCY 315000000U
```

### 16.2 增加频率预设

需要同时修改两处：

1. `main/web_server.c` 中的 `s_freq_presets[]`（API 返回的预设列表）；
2. `web_ui_get_handler()` 中 HTML `<select>` 的 `<option>` 列表（网页下拉框）。

### 16.3 修改引脚

编辑 `main/cc1101_driver.h` 中的 `CC1101_*_GPIO` 宏，并同步修改接线。

### 16.4 调整扫描灵敏度

编辑：

```c
#define CC1101_RSSI_THRESHOLD (-75)
```

数值越接近 0，触发越困难；数值越低，越容易触发，但也更容易把噪声当成信号。

### 16.5 调整最大脉冲数

```c
#define CC1101_MAX_PULSES 512
```

增大后会增加 `cc1101_capture_result_t` 和脉冲文件大小，也会增加任务栈/内存压力。

### 16.6 扩展发射方式

当前发射使用 `esp_rom_delay_us()` 做软件延时。如果需要更精确的时序，可以改为：

- ESP32-S3 RMT 外设；
- 硬件定时器 + GPIO；
- 或使用 CC1101 的异步串行 TX 模式配合精确时钟。

### 16.7 代码入口

- 应用入口：`main/main.c` 的 `app_main()`；
- 初始化顺序：NVS → SPIFFS → Wi-Fi AP → Web 服务 → CC1101；
- CC1101 初始化失败不会阻止 Web 服务启动，状态页会显示未连接。

---

## 17. 辅助脚本说明

仓库中的 Python 脚本属于早期辅助工具，不是固件功能的一部分：

- `demo_cc1101_signal_manager.py`：PC 端模拟演示，不会连接 ESP32 或 CC1101，也不会访问真实 HTTP 接口；
- `setup_and_test.py`：检查工程结构并尝试调用 `idf.py build`，需要先配置好 ESP-IDF 环境；
- `validate_project.py`：早期结构检查脚本，检查规则可能与当前工程不完全同步。

判断工程是否可用，请以以下命令为准：

```bash
idf.py build
```

以及实际烧录后的串口日志和 Web 页面行为。

---

## 18. 版本与许可证

- 当前版本：`v0.0.1`；
- 源代码头部 SPDX 声明：`Unlicense OR CC0-1.0`；
- 仓库中未附带独立的 `LICENSE` 文件，具体以源码头部的 SPDX 声明为准。

---

## 19. 快速检查清单

- [ ] ESP32-S3 供电正常；
- [ ] CC1101 使用 3.3 V，GND 共地；
- [ ] CS=GPIO10、SCLK=GPIO12、MOSI=GPIO11、MISO=GPIO13；
- [ ] GDO0=GPIO9 已连接；
- [ ] 天线与工作频率匹配；
- [ ] `idf.py set-target esp32s3` 已执行；
- [ ] `idf.py build` 成功；
- [ ] `partitions.csv` 已生效（包含 `storage` 分区）；
- [ ] 串口显示 `CC1101 detected`；
- [ ] 能连接 AP `myssid` / `mypassword`；
- [ ] 能打开 `http://192.168.4.1/`；
- [ ] 选择正确频率后点击“开始捕获”；
- [ ] 按下遥控器后能看到检测结果并保存；
- [ ] 点击“发射”后串口输出 `Transmitted N pulses`。
