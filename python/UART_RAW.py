import serial

ser = serial.Serial("COM9", 115200, timeout=1)

while True:
    data = ser.read(20)
    if data:
        print(data.hex(" "))
