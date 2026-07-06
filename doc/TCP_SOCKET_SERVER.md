# TCP Socket Server Test Mode

เอกสารนี้อธิบายโหมด `MODE_TCP_SOCKET_SERVER` สำหรับทดสอบส่งข้อมูลผ่าน Raw TCP จากบอร์ด STM32 + W5500 ไปยังฝั่ง PC และอธิบายโครงสร้าง packet ที่ใช้ร่วมกับสคริปต์ Python

## ภาพรวม

- บอร์ดทำงานเป็น TCP server
- ใช้ W5500 ผ่าน SPI
- ส่ง packet ขนาดคงที่ `40` bytes ทุก `1000 ms`
- ข้อมูลในโหมดนี้เป็นข้อมูลสุ่มสำหรับทดสอบ client
- ฝั่ง PC ใช้สคริปต์ [python/read_battery_tcp.py](python/read_battery_tcp.py) รับและ parse packet

ไฟล์หลักที่เกี่ยวข้อง:

- [src/ModeTcpSocketServer.cpp](src/ModeTcpSocketServer.cpp)
- [include/ModeTcpSocketServer.h](include/ModeTcpSocketServer.h)
- [include/Config.h](include/Config.h)
- [python/read_battery_tcp.py](python/read_battery_tcp.py)

## Network Config

ค่าปัจจุบันอยู่ใน [include/Config.h](include/Config.h)

- `TCP port`: `5000`
- `send interval`: `1000 ms`
- `IP mode`: `static`
- `static IP`: `192.168.0.99`
- `subnet mask`: `255.255.255.0`
- `gateway`: `192.168.0.1`
- `dns`: `192.168.0.1`

ขา W5500 ปัจจุบัน:

- `SCK = PA5`
- `MISO = PB4`
- `MOSI = PB5`
- `CS = PB15`

ถ้าต้องการกลับไปใช้ DHCP ให้แก้ค่า `kTcpSocketUseStaticIp` ใน [include/Config.h](include/Config.h) เป็น `false`

## Packet Layout

ฝั่งเฟิร์มแวร์ประกาศ struct ไว้ใน [include/ModeTcpSocketServer.h](include/ModeTcpSocketServer.h)

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

ขนาดรวม `40 bytes`

### Binary Format

ฝั่ง Python ใช้ format นี้:

```python
struct.Struct("<5f12s2f")
```

ความหมาย:

- `<` = little-endian
- `5f` = `float` 5 ตัวแรก
- `12s` = string/bytes ขนาด 12 bytes
- `2f` = `float` 2 ตัวท้าย

### Field Table

| Offset | Size | Type | Field | Description |
|---|---:|---|---|---|
| 0 | 4 | float | `voltageV` | แรงดันแบตเตอรี่ |
| 4 | 4 | float | `currentA` | กระแสแบตเตอรี่ |
| 8 | 4 | float | `batteryPercent` | เปอร์เซ็นต์แบตเตอรี่ |
| 12 | 4 | float | `temperatureC` | อุณหภูมิ |
| 16 | 4 | float | `humidityPercent` | ความชื้นสัมพัทธ์ |
| 20 | 12 | char[12] | `batState` | สถานะ `charge`, `discharge`, `full` |
| 32 | 4 | float | `remainingUsageTimeHours` | เวลาคงเหลือโดยประมาณ (ชั่วโมง) |
| 36 | 4 | float | `fullChargeEstimateTimeHours` | เวลาชาร์จเต็มโดยประมาณ (ชั่วโมง) |

หมายเหตุ:

- `batState` ถูกส่งเป็น ASCII และเติม `\0` จนครบ 12 bytes
- ฝั่ง Python ตัด `\0` ออกก่อนแปลงเป็นข้อความ
- current ในโหมดทดสอบนี้เป็นค่าที่สุ่มขึ้น ไม่ใช่ค่าจากเซนเซอร์จริง

## Data Ranges In Test Mode

ค่าที่สุ่มจาก [src/ModeTcpSocketServer.cpp](src/ModeTcpSocketServer.cpp) ปัจจุบันมีช่วงประมาณนี้:

- `voltageV`: `24.0 .. 29.2`
- `currentA`:
  - `charge`: `0.2 .. 12.0`
  - `discharge`: `-12.0 .. -0.2`
  - `full`: `-0.15 .. 0.15`
- `batteryPercent`: `5.0 .. 100.0`
  - ถ้า `full`: `99.0 .. 100.0`
- `temperatureC`: `20.0 .. 38.0`
- `humidityPercent`: `35.0 .. 85.0`
- `remainingUsageTimeHours`: `0.25 .. 18.0`
- `fullChargeEstimateTimeHours`: `0.0 .. 12.0`

## Firmware Usage

1. ตั้ง `OPERATING_MODE` ใน [include/Config.h](include/Config.h) เป็น `MODE_TCP_SOCKET_SERVER`
2. ตรวจค่า IP/port ให้ตรงกับวง LAN ที่ต้องการ
3. build และ upload เฟิร์มแวร์
4. เปิด serial monitor เพื่อตรวจ log เริ่มต้น

log ที่ควรเห็นเช่น:

```text
--- TCP Socket Server Test Mode ---
MAC: 02:47:07:00:00:10
TCP port: 5000
Packet size: 40 bytes
IP mode: static
Static IP: 192.168.0.99
Starting Ethernet (Static IP)...
TCP server listening on 192.168.0.99:5000
```

เมื่อมี client เชื่อมต่อ:

```text
TCP client connected: 192.168.0.10
```

และเมื่อส่ง packet:

```text
TCP TX state=charge V=27.45 I=5.23 SoC=74.1 T=31.2 H=58.4 RemH=4.25 FullH=1.80
```

## Python Client Usage

สคริปต์ client อยู่ที่ [python/read_battery_tcp.py](python/read_battery_tcp.py)

ตัวอย่างการรัน:

```bash
python python/read_battery_tcp.py 192.168.0.99 --port 5000
```

ถ้าต้องการดู dataclass ที่ parse แล้ว:

```bash
python python/read_battery_tcp.py 192.168.0.99 --port 5000 --raw
```

ตัวอย่าง output แบบ summary:

```text
V=27.45V I=5.23A SoC=74.1% T=31.20C RH=58.40% State=charge UseLeft=4h 15m ToFull=1h 48m
```

## Troubleshooting

### 1. Python ขึ้น `Connection error: timed out`

ตรวจตามนี้:

- IP ในคำสั่ง Python ต้องตรงกับ IP ที่บอร์ด print ใน serial
- port ต้องเป็น `5000`
- PC กับบอร์ดต้องอยู่ subnet เดียวกัน
- ต้อง upload เฟิร์มแวร์เวอร์ชันล่าสุดที่ใช้ `server.accept()` แล้ว

### 2. Serial ไม่ขึ้น `TCP client connected`

ให้เช็ก:

- สาย LAN และ switch/router
- ค่า static IP ไม่ชนกับอุปกรณ์ตัวอื่น
- subnet/gateway ถูกต้อง
- firewall หรือ network isolation บน PC/Access Point

### 3. Parse packet เพี้ยน

ให้เช็กว่าฝั่ง Python ยังใช้ format เดิม:

```python
PACKET_STRUCT = struct.Struct("<5f12s2f")
```

ถ้ามีการแก้ struct ใน [include/ModeTcpSocketServer.h](include/ModeTcpSocketServer.h) ต้องแก้ parser ฝั่ง Python ตามทุกครั้ง

## ข้อควรระวัง

- โหมดนี้เป็น test mode เท่านั้น ข้อมูลทั้งหมดเป็นข้อมูลสุ่ม
- ถ้าจะเปลี่ยนเป็นข้อมูลจริงจากเซนเซอร์ภายหลัง ต้องแก้ logic ใน [src/ModeTcpSocketServer.cpp](src/ModeTcpSocketServer.cpp)
- ถ้าต้องการ compatibility ระยะยาว แนะนำเพิ่ม version field หรือ sequence number ใน packet รุ่นถัดไป