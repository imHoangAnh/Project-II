# MQTT Client Component

Component này cung cấp khả năng kết nối WiFi và publish dữ liệu BME680 sensor lên MQTT broker.

## 📋 Tính năng

- **WiFi Station Mode**: Tự động kết nối WiFi với retry mechanism
- **MQTT Client**: Publish dữ liệu cảm biến theo format JSON
- **Multiple Topics**: Hỗ trợ nhiều topic cho các loại dữ liệu khác nhau
- **Alert System**: Tự động gửi alert khi chất lượng không khí kém
- **Kconfig Support**: Cấu hình qua `idf.py menuconfig`

## 📡 MQTT Topics

| Topic | Mô tả |
|-------|-------|
| `sensor/bme680/data` | Dữ liệu cảm biến (temperature, humidity, pressure, gas) |
| `sensor/bme680/iaq` | Dữ liệu IAQ (Indoor Air Quality) |
| `sensor/bme680/status` | Trạng thái kết nối (online/offline) |
| `sensor/bme680/alert` | Cảnh báo chất lượng không khí |

## 📦 Payload Format

### Sensor Data (`sensor/bme680/data`)
```json
{
  "temperature": 25.5,
  "humidity": 65.3,
  "pressure": 1013.25,
  "gas_resistance": 50000,
  "gas_valid": true,
  "timestamp": 1704067200
}
```

### IAQ Data (`sensor/bme680/iaq`)
```json
{
  "iaq_score": 75.5,
  "iaq_level": 1,
  "iaq_text": "Good",
  "accuracy": 3,
  "co2_equivalent": 450,
  "voc_equivalent": 0.5,
  "is_calibrated": true,
  "timestamp": 1704067200
}
```

### Status (`sensor/bme680/status`)
```json
{
  "status": "online",
  "client_id": "esp32_bme680_sensor",
  "timestamp": 1704067200
}
```

### Alert (`sensor/bme680/alert`)
```json
{
  "type": "IAQ_ALERT",
  "message": "Air quality is Moderately Polluted! IAQ Score: 175",
  "client_id": "esp32_bme680_sensor",
  "timestamp": 1704067200
}
```

## ⚙️ Cấu hình

### Sử dụng menuconfig

```bash
idf.py menuconfig
```

Navigates đến: `Component config` → `MQTT Client Configuration`

### Các tùy chọn cấu hình:

| Tùy chọn | Mặc định | Mô tả |
|----------|----------|-------|
| `WIFI_SSID` | YOUR_WIFI_SSID | Tên mạng WiFi |
| `WIFI_PASSWORD` | YOUR_WIFI_PASSWORD | Mật khẩu WiFi |
| `WIFI_MAXIMUM_RETRY` | 5 | Số lần retry khi kết nối WiFi thất bại |
| `MQTT_BROKER_URI` | mqtt://192.168.1.100:1883 | URI của MQTT broker |
| `MQTT_CLIENT_ID` | esp32_bme680_sensor | Client ID cho MQTT |

## 🐳 Docker MQTT Broker

### Khởi động Mosquitto broker

```bash
cd ProjectII
docker-compose up -d
```

### Kiểm tra broker

```bash
# Xem log
docker logs mosquitto

# Subscribe test
docker exec -it mosquitto mosquitto_sub -t "sensor/bme680/#" -v

# Publish test
docker exec -it mosquitto mosquitto_pub -t "test" -m "hello"
```

## 🧪 Testing

### Python Subscriber

```bash
# Cài đặt dependencies
pip install paho-mqtt

# Chạy subscriber
python tools/mqtt_subscriber.py --host localhost
```

### MQTT Explorer

Sử dụng [MQTT Explorer](http://mqtt-explorer.com/) để visualize dữ liệu trực quan.

## 🔧 Troubleshooting

### WiFi không kết nối được
- Kiểm tra SSID và password
- Đảm bảo ESP32 trong phạm vi WiFi
- Thử giảm `WIFI_MAXIMUM_RETRY` để nhanh chóng nhận ra lỗi

### MQTT không kết nối được
- Kiểm tra IP address của broker
- Đảm bảo broker đang chạy: `docker ps`
- Kiểm tra firewall không chặn port 1883

### Không nhận được dữ liệu
- Verify cảm biến đang hoạt động qua Serial Monitor
- Kiểm tra topic đúng khi subscribe
- Sử dụng MQTT Explorer để debug

## 📝 API Reference

```c
// Khởi tạo WiFi
esp_err_t wifi_init_sta(void);

// Khởi tạo MQTT client
esp_err_t mqtt_app_init(void);

// Start/Stop MQTT
esp_err_t mqtt_app_start(void);
esp_err_t mqtt_app_stop(void);

// Publish dữ liệu
esp_err_t mqtt_publish_sensor_data(const mqtt_sensor_data_t *data);
esp_err_t mqtt_publish_iaq_data(const mqtt_iaq_data_t *data);
esp_err_t mqtt_publish_status(const char *status);
esp_err_t mqtt_publish_alert(const char *alert_type, const char *message);

// Kiểm tra trạng thái
bool mqtt_is_connected(void);
bool wifi_is_connected(void);
mqtt_status_t mqtt_get_status(void);
```

## 📋 Dependencies

- `esp_wifi`
- `esp_event`
- `esp_netif`
- `mqtt`
- `json`
- `nvs_flash`
