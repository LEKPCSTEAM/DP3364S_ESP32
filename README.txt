DP3364S S-PWM P5 64x32 — โค้ดทดสอบสำหรับ ESP32-S3 (Arduino)
==============================================================

แผงที่รองรับ
  P5-320X160-2121-16S (64x32, 1/16 scan) ชิปคอลัมน์ DP3364S + ชิปแถว ICND1012AP
  ไลบรารี HUB75 DMA ทั่วไปขับแผงนี้ไม่ได้ (จอดับ) เพราะชิปใช้ CLK เป็นนาฬิกา PWM
  ไลบรารีนี้ส่งบิตสตรีมผ่าน LCD_CAM + GDMA ให้ CLK วิ่งต่อเนื่องไม่หยุด

ในไฟล์ zip
  DP3364S_PanelTest/          สเก็ตช์ทดสอบ (.ino) เปิดด้วย Arduino IDE
  libraries/DP3364S/          ไลบรารีขับแผง
  libraries/Adafruit_GFX_Library/  (1.12.6) วาดตัวอักษร/รูปทรง
  libraries/Adafruit_BusIO/        (1.17.4) ที่ Adafruit GFX ต้องใช้

ติดตั้ง
  1. คัดลอกทุกโฟลเดอร์ใน libraries/ ไปไว้ที่
       macOS/Windows: Documents/Arduino/libraries/
     (ถ้ามี Adafruit GFX / BusIO อยู่แล้ว ข้ามสองตัวนั้นได้)
  2. ปิดแล้วเปิด Arduino IDE ใหม่
  3. ต้องมี board package "esp32 by Espressif" (ทดสอบคอมไพล์กับ 3.3.11)

ตั้งค่าบอร์ดใน Arduino IDE (Tools)
  Board            : ESP32S3 Dev Module
  USB CDC On Boot  : Disabled   (USB-C ของบอร์ดต่อผ่านชิป CH343 เป็น UART0)
  Flash Size       : 16MB
  PSRAM            : OPI PSRAM  (ไม่บังคับ ไลบรารีใช้ RAM ภายในเป็นหลัก)
  Port             : /dev/cu.wchusbserial...  (Windows: COMx)
  Serial Monitor   : 115200

ขาที่ใช้ (PX-Board-x3) — แก้ได้ที่ตัวแปร PINS ในไฟล์ .ino
  R1=45 G1=39 B1=48  R2=47 G2=40 B2=21
  A=14 B=41 C=13 D=42  E=ไม่ใช้ (-1)
  LAT=38  OE(ROW)=2  CLK=1

ค่าที่ปรับได้ด้านบนของ .ino
  PANEL_WIDTH   64 ต่อแผง (ต่อพ่วง 2 แผง = 128)
  PANEL_HEIGHT  32
  CLOCK_HZ      8000000 (8 MHz — ค่าที่ทดสอบแล้วว่าติด)
  CLOCK_INVERT  false   (ภาพเพี้ยน/แตก ลอง true)
  BRIGHTNESS    0..255  (ขาวเต็ม 255 กินไฟ ~4 A ต่อแผง)

หน้าจอทดสอบ (เปลี่ยนเองทุก 4 วินาที หรือกดปุ่ม BOOT เพื่อข้าม)
  แดง → เขียว → น้ำเงิน → ขาว → แถบสี → ไล่ระดับ R/G/B/W → ตาราง
  → มุมจอ (แดง=ซ้ายบน เขียว=ขวาบน น้ำเงิน=ซ้ายล่าง ขาว=ขวาล่าง)
  → ไล่แถวบนลงล่าง → ไล่คอลัมน์ซ้ายไปขวา → พลาสม่า → ข้อความวิ่ง
  ชื่อหน้าทดสอบพิมพ์ออก Serial Monitor ทุกครั้งที่เปลี่ยน

ถ้าจอไม่ติด
  1. สาย HUB75 ต้องเสียบช่อง INPUT ของแผง (ลูกศรบนแผงชี้ออกจากช่อง input)
  2. แผงต้องมีไฟ 5V แยก (PSU >= 4 A ต่อแผง) GND ต้องต่อร่วมกับบอร์ด
  3. ดู Serial Monitor: ต้องขึ้น "DP3364S 64x32 running" ถ้าขึ้น init failed ให้ดูข้อความ
  4. ลอง CLOCK_INVERT = true
  ถ้าสีสลับ (เช่น สั่งแดงแต่ขึ้นน้ำเงิน) สลับขา r1/b1 และ r2/b2 ใน PINS

ใช้ในโปรเจกต์ตัวเอง
  #include <DP3364S.h>
  DP3364S panel(64, 32);
  panel.begin(PINS);                 // หรือ begin(PINS, 8000000, false)
  panel.setBrightness(128);
  panel.fillScreen(0);
  panel.setCursor(0, 0); panel.print("Hi");   // ใช้คำสั่ง Adafruit GFX ได้ทั้งหมด
  panel.drawPixelRGB888(x, y, r, g, b);
  panel.show();                      // ต้องเรียกหลังวาดเสร็จทุกเฟรม

สัญญาอนุญาต
  ไลบรารี DP3364S: MIT — โครงบิตสตรีมพอร์ตจาก kkwestt/RaspberryPi-HUB75-DP3364S-Matrix
  (ดู libraries/DP3364S/LICENSE) Adafruit GFX / BusIO: ดู license ในโฟลเดอร์ของแต่ละตัว
