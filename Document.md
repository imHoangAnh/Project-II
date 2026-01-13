# 📄 TÀI LIỆU KỸ THUẬT DỰ ÁN: BME680 ENVIRONMENTAL MONITOR WITH IAQ

## 1. Overview (Tổng quan dự án)
**BME680 Environmental Monitor with IAQ** là hệ thống nhúng giám sát môi trường và chất lượng không khí thời gian thực được phát triển trên nền tảng **ESP32** sử dụng **ESP-IDF** (Espressif IoT Development Framework). 

Hệ thống tập trung vào việc đo đạc chính xác các chỉ số môi trường và **tính toán chỉ số chất lượng không khí trong nhà (IAQ - Indoor Air Quality)** từ dữ liệu cảm biến BME680. Ngoài ra, hệ thống cung cấp cơ chế cảnh báo tự động bằng âm thanh khi phát hiện điều kiện quá nhiệt nguy hiểm.

### Tính năng chính:
- **Giám sát môi trường đa thông số**: Nhiệt độ, Độ ẩm, Áp suất, Khí Gas
- **Tính toán IAQ Score**: Chỉ số chất lượng không khí (0-500)
- **Ước lượng CO2 và VOC**: Dựa trên thuật toán phần mềm
- **Tự động calibration**: Thuật toán tự hiệu chỉnh baseline
- **Lưu trạng thái calibration**: Vào NVS Flash
- **Cảnh báo nhiệt độ**: Còi buzzer khi vượt ngưỡng

## 2. Business Context (Ngữ cảnh nghiệp vụ)
Trong vận hành thiết bị điện tử, phòng server, nhà ở hoặc văn phòng, việc kiểm soát chất lượng không khí và nhiệt độ là yếu tố then chốt để đảm bảo an toàn và sức khỏe.

*   **Vấn đề:** 
    - Các sự cố quá nhiệt thường diễn ra âm thầm và gây hậu quả nghiêm trọng
    - Chất lượng không khí kém ảnh hưởng đến sức khỏe và năng suất làm việc
    - Nồng độ CO2 và VOC cao gây mệt mỏi, đau đầu

*   **Giải pháp:** 
    - Hệ thống giám sát tự động 24/7 với chỉ số IAQ dễ hiểu
    - Thuật toán tính toán IAQ không cần thư viện độc quyền BSEC

*   **Giá trị mang lại:**
    *   **An toàn:** Cảnh báo tức thời giúp ngăn chặn hỏa hoạn
    *   **Sức khỏe:** Theo dõi chất lượng không khí để cải thiện môi trường sống
    *   **Dễ hiểu:** Chỉ số IAQ 0-500 thay vì giá trị Gas Resistance khó hiểu
    *   **Tin cậy:** Hoạt động độc lập, tự động calibration

## 3. System Architecture (Kiến trúc hệ thống)
Hệ thống được thiết kế theo mô hình phân lớp (Layered Architecture) để đảm bảo tính module hóa và dễ bảo trì.

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                         │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────────┐  │
│  │  Sensor Task  │ │  Alert Logic  │ │   Logger/Display  │  │
│  └───────┬───────┘ └───────┬───────┘ └─────────┬─────────┘  │
│          │                 │                   │             │
│  ┌───────┴─────────────────┴───────────────────┴───────┐    │
│  │              IAQ Calculator (Software Algorithm)     │    │
│  │  • IAQ Score Calculation   • CO2/VOC Estimation     │    │
│  │  • Auto-Calibration        • State Persistence      │    │
│  └────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────┤
│                    DRIVER/HAL LAYER                          │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────┐    │
│  │ bme680_app  │ │   buzzer    │ │    i2c_config       │    │
│  └──────┬──────┘ └──────┬──────┘ └──────────┬──────────┘    │
├─────────┼───────────────┼───────────────────┼───────────────┤
│         │     HARDWARE LAYER               │                │
│  ┌──────┴──────┐ ┌──────┴──────┐ ┌─────────┴─────────┐      │
│  │   BME680    │ │   Buzzer    │ │    ESP32/S3       │      │
│  │  (I2C Bus)  │ │   (GPIO)    │ │    (MCU)          │      │
│  └─────────────┘ └─────────────┘ └───────────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### Các thành phần chính:

*   **Hardware Layer (Tầng Phần cứng):**
    *   **MCU:** ESP32/ESP32-S3 (Bộ xử lý trung tâm)
    *   **Sensors:** Bosch BME680 (Cảm biến môi trường tích hợp 4-trong-1)
    *   **Actuators:** Active Buzzer (Còi báo động)
    *   **Interface:** I2C Bus, GPIO

*   **Driver/HAL Layer (Tầng Điều khiển thiết bị):**
    *   `i2c_config`: Quản lý giao tiếp I2C hardware (Clock, Data, Timeout)
    *   `bme680_app`: Thư viện BME68x API từ Bosch + Application wrapper
    *   `buzzer`: Điều khiển GPIO và Task chớp tắt cho còi báo

*   **Application Layer (Tầng Ứng dụng - Business Logic):**
    *   `iaq_calculator`: **[MỚI]** Thuật toán tính toán IAQ, CO2, VOC
    *   `Sensor Task`: Định kỳ đọc và xử lý dữ liệu thô
    *   `Alert Logic`: So sánh dữ liệu với ngưỡng để ra quyết định cảnh báo
    *   `Logger`: Định dạng và xuất dữ liệu ra Console/Terminal

## 4. Hardware Specifications (Đặc tả phần cứng)

### 4.1. Pinmap Configuration (Cấu hình chân nối)
| Component | Chân thiết bị | ESP32 GPIO | Ghi chú |
| :--- | :--- | :--- | :--- |
| **BME680** | SDA | `GPIO 6` | Dây dữ liệu I2C |
| | SCL | `GPIO 7` | Dây xung nhịp I2C |
| | VCC | 3.3V | Nguồn cấp ổn định |
| | GND | GND | Nối đất chung |
| **Buzzer** | POS (+) | `GPIO 2` | Kích mức cao (High) |
| | NEG (-) | GND | |

### 4.2. Giao thức truyền thông
*   **Giao thức:** I2C (Inter-Integrated Circuit)
*   **Tần số (Frequency):** 100 kHz (Standard Mode)
*   **Địa chỉ I2C (Address):** `0x77` (Cấu hình mặc định cho BME680)

## 5. IAQ Calculator - Indoor Air Quality (Chỉ số Chất lượng Không khí)

### 5.1. Tổng quan về IAQ
IAQ (Indoor Air Quality) là chỉ số đánh giá chất lượng không khí trong nhà, được tính toán từ giá trị Gas Resistance của cảm biến BME680.

**Ưu điểm của module IAQ Calculator:**
- ✅ **Không cần thư viện BSEC độc quyền** của Bosch
- ✅ **Open-source algorithm** - Dễ hiểu và tùy chỉnh
- ✅ **Tự động calibration** - Không cần can thiệp thủ công
- ✅ **Persistent state** - Lưu trạng thái vào NVS Flash

### 5.2. Thang đo IAQ Score (0-500)

| IAQ Score | Mức độ | Mô tả | Màu hiển thị |
|:---------:|:-------|:------|:------------:|
| 0-50 | **Excellent** | Chất lượng không khí tuyệt vời | 🟢 Xanh lá |
| 51-100 | **Good** | Chất lượng không khí tốt | 🟢 Xanh nhạt |
| 101-150 | **Lightly Polluted** | Ô nhiễm nhẹ | 🟡 Vàng |
| 151-200 | **Moderately Polluted** | Ô nhiễm trung bình | 🟠 Cam |
| 201-300 | **Heavily Polluted** | Ô nhiễm nặng | 🔴 Đỏ |
| 301-500 | **Severely Polluted** | Ô nhiễm nghiêm trọng | 🟣 Tím |

### 5.3. Trạng thái Calibration (Accuracy)

| Accuracy Level | Mô tả | Hành động khuyến nghị |
|:---------------|:------|:----------------------|
| `Unreliable` | Sensor đang khởi động, chưa ổn định | Chờ 1-2 phút |
| `Low` | Đang trong giai đoạn burn-in | Chờ thêm 5-10 phút |
| `Medium` | Calibration đang tiến hành | Chờ hoàn thành |
| `High` | Đã calibrate đầy đủ, dữ liệu tin cậy | Sẵn sàng sử dụng |

### 5.4. Các chỉ số đầu ra

| Chỉ số | Đơn vị | Mô tả |
|:-------|:-------|:------|
| **IAQ Score** | 0-500 | Chỉ số chất lượng không khí tổng hợp |
| **CO2 Equivalent** | ppm | Ước lượng nồng độ CO2 (400-2000 ppm) |
| **VOC Equivalent** | ppm | Ước lượng nồng độ VOC (0-10 ppm) |
| **Gas Baseline** | Ohms | Giá trị baseline đã calibrate |

### 5.5. Thuật toán IAQ

```
┌─────────────────────────────────────────────────────────────┐
│                    IAQ CALCULATION FLOW                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Raw Data (T, H, P, Gas_R)                                  │
│          │                                                   │
│          ▼                                                   │
│  ┌───────────────────────────────────┐                      │
│  │  Temperature/Humidity Compensation │                      │
│  │  comp_gas = Gas_R × T_factor × H_factor                  │
│  └───────────────────┬───────────────┘                      │
│                      │                                       │
│                      ▼                                       │
│  ┌───────────────────────────────────┐                      │
│  │     Update Gas Baseline (EMA)     │                      │
│  │  baseline = baseline × (1-α) + comp_gas × α              │
│  └───────────────────┬───────────────┘                      │
│                      │                                       │
│                      ▼                                       │
│  ┌───────────────────────────────────┐                      │
│  │     Calculate Gas Ratio           │                      │
│  │     ratio = comp_gas / baseline   │                      │
│  └───────────────────┬───────────────┘                      │
│                      │                                       │
│                      ▼                                       │
│  ┌───────────────────────────────────┐                      │
│  │     Map Ratio to IAQ Score        │                      │
│  │     ratio ≥ 1.0 → IAQ 0-50       │                      │
│  │     ratio 0.5-1.0 → IAQ 50-150   │                      │
│  │     ratio 0.2-0.5 → IAQ 150-250  │                      │
│  │     ratio < 0.2 → IAQ 250-500    │                      │
│  └───────────────────┬───────────────┘                      │
│                      │                                       │
│                      ▼                                       │
│  ┌───────────────────────────────────┐                      │
│  │   Estimate CO2/VOC Equivalents    │                      │
│  │   CO2 = 400 + IAQ × 5 (ppm)      │                      │
│  │   VOC = f(ratio) (ppm)           │                      │
│  └───────────────────────────────────┘                      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## 6. Functional Requirements (Yêu cầu chức năng)

### 6.1. Giám sát môi trường (Monitoring)
*   Hệ thống đọc dữ liệu từ cảm biến với chu kỳ **3000ms (3 giây)/lần**
*   Các thông số cần đo:

| # | Thông số | Đơn vị | Mô tả |
|:-:|:---------|:-------|:------|
| 1 | **Nhiệt độ** | °C | Temperature |
| 2 | **Độ ẩm** | % r.H | Relative Humidity |
| 3 | **Áp suất** | hPa | Atmospheric Pressure |
| 4 | **Gas Resistance** | Ohms | Raw gas sensor value |
| 5 | **IAQ Score** | 0-500 | Indoor Air Quality Index |
| 6 | **CO2 Equivalent** | ppm | Estimated CO2 concentration |
| 7 | **VOC Equivalent** | ppm | Estimated VOC concentration |

### 6.2. Hệ thống cảnh báo (Alert System)

#### Cảnh báo Nhiệt độ:
Hệ thống so sánh nhiệt độ thực tế (`T`) với ngưỡng an toàn (`Th` = 100°C):

| Trạng thái | Điều kiện | Hành động Buzzer | Log hiển thị |
| :--- | :--- | :--- | :--- |
| **Bình thường (Normal)** | `T <= 80°C` | OFF | `Status: Normal` |
| **Cảnh báo sớm (Warning)** | `80°C < T <= 100°C` | OFF | `WARNING: Temperature approaching threshold!` (Vàng) |
| **Báo động (ALERT)** | `T > 100°C` | **ON** (Kêu ngắt quãng) | `ALERT: Temperature exceeds threshold!` (Đỏ) |

#### Cảnh báo IAQ (mở rộng tương lai):
| IAQ Score | Mức độ | Khuyến nghị |
|:---------:|:-------|:------------|
| 0-100 | Tốt | Không cần hành động |
| 101-200 | Trung bình | Cân nhắc thông gió |
| 201-300 | Kém | Cần thông gió ngay |
| 301+ | Nguy hiểm | Cảnh báo và sơ tán |

## 7. Non-Functional Requirements (Yêu cầu phi chức năng)

*   **Real-time:** Sử dụng FreeRTOS Tasks để đảm bảo việc đọc cảm biến và báo động diễn ra song song, không chặn (non-blocking) lẫn nhau
*   **Stability:** Xử lý lỗi (Error Handling) khi mất kết nối cảm biến -> Tự động báo lỗi "Failed to read sensor data" thay vì treo hệ thống
*   **Thread-Safety:** Sử dụng Mutex để bảo vệ biến dữ liệu chia sẻ khi truy cập từ nhiều luồng khác nhau
*   **Persistence:** Lưu trạng thái calibration IAQ vào NVS Flash, tự động khôi phục khi khởi động lại
*   **Accuracy:** Thuật toán tự động calibrate dựa trên Exponential Moving Average (EMA)

## 8. Project Structure (Cấu trúc dự án)
Dự án tuân theo tiêu chuẩn component của ESP-IDF:

```text
ProjectII/
├── CMakeLists.txt              # Cấu hình Build System gốc
├── Document.md                 # Tài liệu dự án (file này)
├── README.md                   # Hướng dẫn nhanh
├── sdkconfig                   # Cấu hình Kconfig của dự án
├── sdkconfig.defaults          # Cấu hình mặc định
│
├── components/                 # Thư viện & Driver tự viết
│   ├── bme680/                 # BME68x Driver gốc từ Bosch
│   │   ├── bme68x.c           # Core driver implementation
│   │   ├── bme68x.h           # Header definitions
│   │   └── bme68x_defs.h      # Type & constant definitions
│   │
│   ├── bme680_app/             # BME680 Application wrapper
│   │   ├── bme680_app.c       # Init, Read, Thread-safe data
│   │   ├── bme680_app.h       # Public API
│   │   └── CMakeLists.txt     # Component build config
│   │
│   ├── buzzer/                 # Buzzer driver
│   │   ├── buzzer.c           # GPIO & Task implementation
│   │   ├── buzzer.h           # Public API
│   │   └── CMakeLists.txt     # Component build config
│   │
│   ├── i2c_config/             # I2C Master configuration
│   │   ├── i2c_config.c       # I2C init & helpers
│   │   ├── i2c_config.h       # Pin definitions & API
│   │   └── CMakeLists.txt     # Component build config
│   │
│   └── iaq_calculator/         # [MỚI] IAQ Calculation module
│       ├── iaq_calculator.c   # Core algorithm implementation
│       ├── iaq_calculator.h   # Public API & data types
│       └── CMakeLists.txt     # Component build config
│
└── main/                       # Source code chính
    ├── main.c                  # Entry point (app_main), khởi tạo & chạy Task
    └── CMakeLists.txt          # Cấu hình Build của main
```

## 9. Workflow Details (Luồng hoạt động chi tiết)

### 9.1. Khởi động (Startup)
```
1. Power ON
   │
   ▼
2. NVS Flash Init
   │
   ▼
3. Create Sensor Mutex
   │
   ▼
4. Initialize Buzzer (GPIO 2)  ──► Buzzer OFF
   │
   ▼
5. Initialize I2C (GPIO 6, 7)
   │
   ▼
6. Initialize BME680
   │  • Soft reset
   │  • Check Chip ID (0x61)
   │  • Load calibration data
   │  • Configure oversampling
   │  • Setup gas heater (320°C, 150ms)
   │
   ▼
7. Initialize IAQ Calculator   ◄── [MỚI]
   │  • Create mutex
   │  • Try load saved state from NVS
   │  • Initialize baseline
   │
   ▼
8. Start Tasks
   │  • sensor_task (stack: 4096 words)
   │  • buzzer_task
   │
   ▼
9. System Ready ✓
```

### 9.2. Vòng lặp chính (Runtime Loop)
Quy trình lặp lại mỗi 3 giây trong `sensor_task`:

```
┌─────────────────────────────────────────────────────────────┐
│                    SENSOR TASK LOOP                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Thu thập dữ liệu thô                                    │
│     • Set BME680 → Forced Mode                              │
│     • Wait for measurement (∼170ms)                         │
│     • Read T, H, P, Gas_R                                   │
│                     │                                        │
│                     ▼                                        │
│  2. Cập nhật dữ liệu (Thread-safe)                          │
│     • Lock Mutex                                            │
│     • Update global sensor data                             │
│     • Increment read counter                                │
│     • Unlock Mutex                                          │
│                     │                                        │
│                     ▼                                        │
│  3. Tính toán IAQ  ◄── [MỚI]                                │
│     • Prepare iaq_raw_data_t                                │
│     • Call iaq_calculate()                                  │
│     │  ├─ Compensate gas for T/H                           │
│     │  ├─ Update gas baseline (EMA)                        │
│     │  ├─ Calculate IAQ score                              │
│     │  └─ Estimate CO2/VOC                                 │
│     │                                                        │
│                     │                                        │
│                     ▼                                        │
│  4. Hiển thị kết quả                                        │
│     ╔════════════════════════════════════════════════════╗  │
│     ║  BME680 SENSOR DATA - Reading #123                 ║  │
│     ╠════════════════════════════════════════════════════╣  │
│     ║  Temperature :    25.50 °C                         ║  │
│     ║  Humidity    :    45.20 %                          ║  │
│     ║  Pressure    :  1013.25 hPa                        ║  │
│     ║  Gas Resist. :   150000 Ohms                       ║  │
│     ╠════════════════════════════════════════════════════╣  │
│     ║  ─── AIR QUALITY (IAQ) ───                         ║  │
│     ║  IAQ Score   :    35.2   [Excellent]               ║  │
│     ║  CO2 Equiv.  :      576  ppm                       ║  │
│     ║  VOC Equiv.  :     0.15  ppm                       ║  │
│     ║  Accuracy    : High (Calibrated)                   ║  │
│     ╠════════════════════════════════════════════════════╣  │
│     ║  Status: Normal                                    ║  │
│     ╚════════════════════════════════════════════════════╝  │
│                     │                                        │
│                     ▼                                        │
│  5. Kiểm tra ngưỡng nhiệt độ                                │
│     • T > 100°C  → buzzer_set_active(true)                 │
│     • T <= 100°C → buzzer_set_active(false)                │
│                     │                                        │
│                     ▼                                        │
│  6. Lưu trạng thái IAQ (mỗi 20 lần đọc)                     │
│     • Nếu đã calibrated → iaq_save_state()                 │
│                     │                                        │
│                     ▼                                        │
│  7. Nghỉ                                                    │
│     • vTaskDelay(3000ms)                                    │
│     • Quay lại bước 1                                       │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## 10. API Reference (Tham chiếu API)

### 10.1. IAQ Calculator API

```c
// Khởi tạo IAQ calculator với cấu hình mặc định
esp_err_t iaq_init(void);

// Khởi tạo với cấu hình tùy chỉnh
esp_err_t iaq_init_with_config(const iaq_config_t *config);

// Tính toán IAQ từ dữ liệu thô
esp_err_t iaq_calculate(const iaq_raw_data_t *raw_data, iaq_result_t *result);

// Lấy kết quả IAQ gần nhất (thread-safe)
esp_err_t iaq_get_result(iaq_result_t *result);

// Reset thuật toán (bắt đầu calibration lại)
void iaq_reset(void);

// Lưu/Load trạng thái từ NVS
esp_err_t iaq_save_state(void);
esp_err_t iaq_load_state(void);

// Helper functions
const char *iaq_level_to_string(iaq_level_t level);
const char *iaq_accuracy_to_string(iaq_accuracy_t accuracy);
uint32_t iaq_level_to_color(iaq_level_t level);
bool iaq_is_calibrated(void);
uint8_t iaq_get_calibration_progress(void);
```

### 10.2. Data Structures

```c
// Dữ liệu thô đầu vào
typedef struct {
  float temperature;      // Nhiệt độ °C
  float humidity;         // Độ ẩm %
  float pressure;         // Áp suất Pa
  float gas_resistance;   // Gas resistance Ohms
  bool gas_valid;         // Cờ validity
} iaq_raw_data_t;

// Kết quả IAQ
typedef struct {
  float iaq_score;              // IAQ index (0-500)
  iaq_level_t iaq_level;        // Mức phân loại
  iaq_accuracy_t accuracy;      // Độ chính xác
  
  float co2_equivalent;         // CO2 ước lượng (ppm)
  float voc_equivalent;         // VOC ước lượng (ppm)
  
  float gas_baseline;           // Baseline hiện tại
  uint32_t samples_count;       // Số mẫu đã xử lý
  bool is_calibrated;           // Đã calibrate xong
} iaq_result_t;
```

## 11. Hướng dẫn Build & Flash

### Yêu cầu
*   **Phần mềm:** ESP-IDF v5.0 trở lên
*   **Driver:** USB-UART Driver (CP210x/CH340) cho ESP32

### Lệnh thực thi
Tại thư mục gốc dự án, chạy lần lượt các lệnh:

1.  **Cấu hình dự án (nếu cần đổi chip/port):**
    ```bash
    idf.py set-target esp32s3
    idf.py menuconfig
    ```

2.  **Biên dịch mã nguồn:**
    ```bash
    idf.py build
    ```

3.  **Nạp code xuống mạch & Giám sát:**
    ```bash
    idf.py -p COMx flash monitor
    ```
    *(Thay `COMx` bằng cổng COM thực tế, ví dụ `COM3` hoặc `/dev/ttyUSB0`)*

## 12. Troubleshooting (Xử lý sự cố)

### IAQ không hiển thị
| Triệu chứng | Nguyên nhân | Giải pháp |
|:------------|:------------|:----------|
| "Waiting for valid gas data" | Gas measurement invalid | Kiểm tra sensor, chờ heater warm-up |
| IAQ = 0, Accuracy = Unreliable | Sensor đang chạy lần đầu | Chờ 1-2 phút để stabilize |
| IAQ không thay đổi | Baseline chưa calibrate | Đợi đủ 50 samples (~150s) |

### Calibration issues
| Triệu chứng | Nguyên nhân | Giải pháp |
|:------------|:------------|:----------|
| Accuracy không lên High | Môi trường biến động nhiều | Để sensor ở nơi ổn định |
| IAQ luôn ở mức cao | Baseline bị lệch | Gọi `iaq_reset()` để calibrate lại |
| State không được lưu | NVS lỗi | Kiểm tra `nvs_flash_init()` |

---

## Changelog

| Version | Date | Changes |
|:--------|:-----|:--------|
| 1.0 | - | Initial release with BME680 basic monitoring |
| 2.0 | 2026-01-12 | Added IAQ Calculator module |
|     |            | - Software-based IAQ algorithm |
|     |            | - CO2 and VOC estimation |
|     |            | - Auto-calibration with NVS persistence |
|     |            | - Updated documentation |

---
*Document Version: 2.0 - Generated by AI Assistant*
*Last Updated: 2026-01-12*
