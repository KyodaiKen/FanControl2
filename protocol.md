# Kyoudai Ken FC02 COM protocol documentation
## Sending commands
1. Send the command byte followed by 0x0A to terminate it with endline
2. If you are setting something, send the corresponding data points (curve or matrix) and terminate each entry with endline (0x0A) / otherwise await data sent in binary terminated by 0x0A endline

## Number of curves entries is fixed to
```
10
```
You have to send 10 and also receive 10. If you use less than 10 curve points, you just enter the last value pair repeatedly.
## Curve data format / repeat for eache curve entry
FLOAT32 and BYTE pairs (5 byte per entry)
```
FLOAT32 control value from matrix
BYTE    fan PWM control value
[0x0A]
```

## Matrix data format
3 FLOAT32 values
```
FLOAT32 - Sensor 0
FLOAT32 - Sensor 1
FLOAT32 - Sensor 3
[0x0A]
```

## Sensor data format
```
FLOAT32 - Temperature Sensor 0
FLOAT32 - Temperature Sensor 1
FLOAT32 - Temperature Sensor 2
[0x0A]
FLOAT32 - Matrix value channel 0
FLOAT32 - Matrix value channel 1
FLOAT32 - Matrix value channel 2
[0x0A]
FLOAT32 - PWM duty cycle channel 0
FLOAT32 - PWM duty cycle channel 1
FLOAT32 - PWM duty cycle channel 2
[0x0A]
```

## Commands
### Range 0xA0 - 0xA2 | Get curve data
For the format, see above
```
0xA0 - Get curve 0
0xA1 - Get curve 1
0xA2 - Get curve 2
```

### Range 0xB0 - 0xB2 | Set curve data
For the format, see above
```
0xB0 - Set curve 0
0xB1 - Set curve 1
0xB2 - Set curve 2
```

### Range 0xC0 - 0xC2 | Get matrix data
For the format, see above
```
0xC0 - Get matrix for fan bank 0
0xC1 - Get matrix for fan bank 1
0xC2 - Get matrix for fan bank 2
```

### Range 0xD0 - 0xD2 | Set matrix data
For the format, see above
```
0xD0 - Set matrix for fan bank 0
0xD1 - Set matrix for fan bank 1
0xD2 - Set matrix for fan bank 2
```

### Range 0xE0 - 0xFF | Commands
```
0xE0 - Get identifier / Version
0xEE - Write to EEPROM
0xFE - Restore from EEPROM / RESET
0xF0 - Test fan speed of bank 0 specified by the speed using a BYTE 0 .. 100 for 5 seconds
0xF1 - Test fan speed of bank 1 specified by the speed using a BYTE 0 .. 100 for 5 seconds
0xF2 - Test fan speed of bank 2 specified by the speed using a BYTE 0 .. 100 for 5 seconds
0xFA - Get sensor data (See Sensor Data Format)
```