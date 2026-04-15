# ESP32-S3-Eye Fluid Display

基于 ESP32-S3-Eye 开发板的流体模拟显示项目。通过加速度计检测设备倾斜方向，控制虚拟流体的流动。

## 功能特性

- 实时流体粒子模拟
- 加速度计倾斜检测（支持 MPU6050）
- 240x240 LCD 显示
- 粒子物理效果（重力、摩擦力、粒子间相互作用）
- 可配置参数（通过 menuconfig）

## 硬件要求

- ESP32-S3-Eye 开发板
- MPU6050 加速度计模块（可选，支持模拟模式）
  - I2C SDA: GPIO 18
  - I2C SCL: GPIO 19

## 软件要求

- ESP-IDF v5.4.3
- CMake 3.16+
- Python 3.8+

## 构建和烧录

### 1. 设置 ESP-IDF 环境

```bash
# Windows
cd C:\Espressif\frameworks\esp-idf-v5.4.3
install.bat
export.bat

# 或者 Linux/Mac
cd ~/esp/esp-idf
./install.sh
. ./export.sh
```

### 2. 配置项目

```bash
cd examples/esp32-s3-eye-fluid-display
idf.py set-target esp32s3
idf.py menuconfig
```

在 menuconfig 中可以配置：
- `Fluid Display Example Configuration` - 流体显示配置
  - 启用/禁用模拟模式
  - 最大粒子数量
  - 更新间隔
  - I2C 引脚配置

### 3. 编译和烧录

```bash
# 编译
idf.py build

# 烧录到开发板
idf.py -p PORT flash monitor
```

将 `PORT` 替换为实际的串口端口号，例如：
- Windows: `COM3`, `COM4` 等
- Linux: `/dev/ttyUSB0`, `/dev/ttyACM0` 等
- Mac: `/dev/tty.usbserial-*`

## 项目结构

```
esp32-s3-eye-fluid-display/
├── CMakeLists.txt          # 项目 CMake 配置
├── Kconfig.projbuild       # menuconfig 配置
├── README.md               # 本文件
└── main/
    ├── CMakeLists.txt      # main 目录 CMake 配置
    ├── main.c              # 主程序入口
    ├── lcd_driver.h        # LCD 驱动头文件
    ├── lcd_driver.c        # LCD 驱动实现
    ├── accelerometer.h     # 加速度计头文件
    ├── accelerometer.c     # 加速度计实现
    ├── fluid_simulation.h  # 流体模拟头文件
└── fluid_simulation.c      # 流体模拟实现
```

## 模块说明

### LCD 驱动模块 (lcd_driver)

提供 LCD 显示的基本操作：
- `lcd_init()` - 初始化 LCD
- `lcd_fill_screen()` - 填充屏幕
- `lcd_draw_pixel()` - 绘制像素
- `lcd_fill_circle()` - 绘制填充圆
- `lcd_draw_string()` - 绘制字符串

### 加速度计模块 (accelerometer)

提供加速度计数据读取：
- `accelerometer_init()` - 初始化加速度计
- `accelerometer_read()` - 读取加速度数据
- `accelerometer_deinit()` - 关闭加速度计

支持自动检测 MPU6050，如果未检测到硬件则使用模拟数据。

### 流体模拟模块 (fluid_simulation)

提供流体粒子模拟：
- `fluid_init()` - 初始化流体模拟器
- `fluid_set_flow_direction()` - 设置流动方向
- `fluid_update()` - 更新模拟状态
- `fluid_render()` - 渲染到 LCD
- `fluid_clear()` - 清除所有粒子

## 工作原理

1. 系统初始化 LCD 显示和加速度计
2. 主循环中：
   - 读取加速度计数据，获取设备倾斜方向
   - 根据倾斜方向设置流体流动方向
   - 更新流体粒子物理状态
   - 渲染流体到 LCD 屏幕
3. 重复步骤 2

## 模拟模式

如果未连接 MPU6050 加速度计，系统会自动切换到模拟模式：
- 使用正弦/余弦函数生成模拟加速度数据
- 流体仍然会流动，但方向会周期性变化

## 性能优化建议

1. 减少粒子数量可以降低 CPU 使用率
2. 增加更新间隔可以降低功耗
3. 使用 PSRAM 可以支持更多粒子

## 故障排除

### 无法检测到 MPU6050

- 检查 I2C 接线是否正确
- 检查电源连接
- 使用模拟模式测试

### 显示异常

- 检查 LCD 初始化参数
- 确认屏幕分辨率设置正确

### 编译错误

- 确保 ESP-IDF 环境已正确设置
- 运行 `idf.py clean` 清理后重新编译

## 许可证

本项目遵循 MIT 许可证。

## 参考资源

- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/)
- [ESP32-S3-Eye 开发板文档](https://github.com/espressif/esp-box)
- [MPU6050 数据手册](https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/)