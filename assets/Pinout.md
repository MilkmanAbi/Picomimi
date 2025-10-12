# RP2040 v0.1 to v8 / Picomimi - Hardware Wiring

## ILI9341 240x320 3.2" Screen

VCC  ->  3.3V  
GND  ->  GND  
CS   ->  GPIO 21  
RESET -> GPIO 20  
DC   ->  GPIO 17  
SDI (MOSI) -> GPIO 19  
SCK  ->  GPIO 18  
LED  ->  GPIO 22  
SD0 (MISO) -> GPIO 16  

### Resistive Touch Screen

T_CLK -> GPIO 10  
T_CS  -> GPIO 11  
T_DIN -> GPIO 12  
T_D0  -> GPIO 13  
T_IRQ -> GPIO 14  

---

## SD Card

> Shares SPI with the display

SD_CS  -> GPIO 5  
SD_MOSI -> GPIO 19  
SD_MISO -> GPIO 16  
SD_SCK  -> GPIO 18  

---

## Push Buttons

> Connect to GND and the corresponding GPIO

Left   -> GPIO 3  
Right  -> GPIO 1  
Top    -> GPIO 0  
Bottom -> GPIO 2  
Select -> GPIO 4  
Start  -> GPIO 6  
A      -> GPIO 7  
B      -> GPIO 8  
ON_OFF -> GPIO 9  

---

**Note:** You don’t need all components connected for the system to work. Missing connections/hardware will simply be ignored by the Picomimi system. Picomimi will work on any RP2040 out of the box.
**:)**
