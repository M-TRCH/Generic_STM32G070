# TCP Socket Server

โหมด `MODE_TCP_SOCKET_SERVER` ทำให้บอร์ดเป็น Raw TCP server และส่ง packet binary ขนาดคงที่ให้ client ทุก `1000 ms`

ไฟล์อ้างอิง:

- [src/ModeTcpSocketServer.cpp](src/ModeTcpSocketServer.cpp)
- [include/ModeTcpSocketServer.h](include/ModeTcpSocketServer.h)
- [include/Config.h](include/Config.h)
- [python/read_battery_tcp.py](python/read_battery_tcp.py)

## TCP Server

- `port`: `5000`
- `IP mode`: `static`
- `IP`: `192.168.0.99`
- `subnet`: `255.255.255.0`
- `gateway`: `192.168.0.1`
- `dns`: `192.168.0.1`

W5500 SPI:

- `SCK = PA5`
- `MISO = PB4`
- `MOSI = PB5`
- `CS = PB15`

ถ้าจะเปลี่ยน network config ให้แก้ใน [include/Config.h](include/Config.h)

## Packet Struct

ฝั่งเฟิร์มแวร์ส่ง struct นี้แบบ packed:

```cpp
#pragma pack(push, 1)
struct BatteryTelemetryTcpPacket
{
  float voltageV;
  float currentA;
  float batteryPercent;
  float temperatureC;
  float humidityPercent;
  char batState[12];
  float remainingUsageTimeHours;
  float fullChargeEstimateTimeHours;
};
#pragma pack(pop)
```

- ขนาดรวม: `40 bytes`
- byte order: `little-endian`
- Python parser: `struct.Struct("<5f12s2f")`

| Offset | Size | Type | Field |
|---|---:|---|---|
| 0 | 4 | float | `voltageV` |
| 4 | 4 | float | `currentA` |
| 8 | 4 | float | `batteryPercent` |
| 12 | 4 | float | `temperatureC` |
| 16 | 4 | float | `humidityPercent` |
| 20 | 12 | char[12] | `batState` |
| 32 | 4 | float | `remainingUsageTimeHours` |
| 36 | 4 | float | `fullChargeEstimateTimeHours` |

`batState` เป็น ASCII string และค่าที่ใช้ตอนนี้คือ `charge`, `discharge`, `full`

## Usage

ตั้งโหมดใน [include/Config.h](include/Config.h) เป็น `MODE_TCP_SOCKET_SERVER` แล้ว build/upload firmware

ฝั่ง Python:

```bash
python python/read_battery_tcp.py 192.168.0.99 --port 5000
```

หรือดูค่าดิบหลัง parse:

```bash
python python/read_battery_tcp.py 192.168.0.99 --port 5000 --raw
```

หมายเหตุ: โหมดนี้ส่งข้อมูลสุ่มสำหรับทดสอบ ไม่ใช่ข้อมูลจากเซนเซอร์จริง