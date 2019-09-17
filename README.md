# FanControl2
This is a DIY project that has also a PCB circuit and design on EasyEDA. It will be published later as I have to redesign it.
## Description
This project is for water cooling enthusiasts. Be it custom loop or AiO, the fan control of motherboard leaves a lot to be desired.

For example, when the system is busy, the software of the motherboard manufacturers might stall and your fan speed is not being updated anymore which can lead to overheating issues especially when overclocking.

## What can this piece of firmware do for me?
This software can read three NTC thermistors of any kind. You just need to adjust the resistor values and the Steinhart-Hart coefficients to calibrate those. With this information, the controller outputs three fan speed PWM signals (25 KHz) which can be used with any PWM controllable PC fan! You can find the pin details in the code. Until I release the schematic and PCB layout, you can find tutorials on how to connect them via DuckDuckGo, Startpage or whatever you tend to use.

### Mix and match temperature sensors to your liking!

You can program a custom sensor to curve correlation matrix!

This is the default matrix:
```
Curve  t0   t1   t2
0      1    0    0
1      0    1    0
2      0    0    1
```

Let's say your sensor t0 is your water temperature, t1 is your ambient temperature and t2 is your inner case temperature. You can then modify this matrix to mix temperature readings to your desired curve. Let's do this with the Curve 0 for the radiator fan array speed!

Assuming
* curve 0 is your radiator fan array fan speed
* curve 1 is your case fan array fan speed
* curve 2 is not used

Reference to set matrix: `sm <curve 1...2> <matrix value> <matrix value> <matrix value>`

#### Example
You enter:
```
sm 0 1 -1 0
sm 1 0 0 0
sm 2 0 1 -1
```
* Curve 0 (radiator fans) will now receive the delta over ambient temperature. In case of sensor readings when the PC is cold, negative values due to sensor fluctuations will be clamped to 0. This is generally true for this matrix. A negative result will be handled as 0°C. This controller is NOT for sub zero cooling, which is impractical anyway.

* Curve 2 is always 0°C, cause it's not being used.

* Curve 2 (case fans) will receive a pseudo temperature of the three sensors weighted towards the inner case temperature, but also taking into account the temperature of your water and ambient. Using testing you can craft yourself a curve that suits your needs!

#### Check programmed matrix
`gm <channel/curve>` returns the contents of your matrix.

#### Matrix error messages
* `e0` - The curve matrix you want to store does not exist. Enter a number between 0 and 2.
* `e1` - The matrix must consist of 3 values separated by space.

### How do I program fan curves, though?
You can now program curves via COM interface into it.

You open a com console and send
`sc <curve 0...2> <x y x y x y x y ... space separated>` to program a curve.
*For example*
```
sc 0 0 0 1 15 2 20 3 35 99 35
```
The first 0 is the curve ID, more on that below. The next numbers separated by spaces are `x y x y x y` representations of the curve coordinates. X is the temperature, y is the fan speed.

Each curve is for a sensor. Curve 0 is water temperature over ambient for the rad fans, curve 1 is reserved for another temperature sensor and fan header and curve 2 is ambient and case fans. Points are linearily interpolated inbetween, so when you have an off temperature the fan speed will be extrapolated between the 2 nearest points in a linear fashion. If you want to change how it works, you can change the code and flash it again!

`gc <curve>` lists the points of your curve you've programmed.

You can program up to **158** curve points x and y which gives you **79** xy coordinates for temp (x) and fan speed (y) per curve!
This number resulted from the EEPROM in Arduino Nano having a whopping **512 bytes** of storage.

Send `gs` and you get sensor readings and fan speeds:

```
22.0 20.3
!NAN 100.0
20.0 23.9
```

First number is temperature, second is fan speed in percent! I will get a different controller board, make a new PCB and stuff to also measure the fan speed in RPM soon! One each channel.

Send `psc` and it goes into a mode where it spits out the same sensor data as above every second and you can stop this mode with !psc.

#### Curve error messages
* `e0` - The curve/matrix you want to store does not exist. Enter a number between 0 and 2.
* `e1` - Curve points are not even, you need 2 pairs of values for x and y data points.
* `e2` - The number of curve points exceeds the maximum (84) that the EEPROM can store for each curve.
* `e3` - Every curve must start with a 0 degree temperature fan speed.
* `e4` - The curve data is invalid. The maximum temperature is 100°C, the maximum fan speed is 100 percent.

### General error messages
* `EEPROM BAD` - When you use a new controller and flash this firmware, there could be garbage in it's EEPROM and the data plausability check detected it. Simple default curves were loaded and you are prompted to send your custom ones.

## Testing curves and matrix
You can test the matrix and curves you just set up:

### Test matrix
`tm <curve> <sensor 0> <sensor 1> <sensor 2>`
#### Example
`tm 0 20 0 18`
```
m   2.00
s  20.00
```

Explanation:
* `m` - Matrix value
* `s` - Resulting fan speed duty cycle

### Test curve
`tc <curve> <matrix value>`
#### Example
`tc 0 2`
```
s  20
```
Explanation:
* `s` - Resulting fan speed duty cycle

## Future plans
### CRC16 for the EEPROM data
There are two free bytes that could be used for a CRC16 of the data stored in the EEPROM.

### UI for your PC
It's planned but not yet finished.
You can use PuTTy or other terminal programs to send and receive commands via the Arduino's COM interface. The speed is 9600 baud and the rest is default.

### LibreHardwareMonitor integration
I plan to integrate the sensor readings into LibreHardwareMonitor, too. I will discuss it with the developers of LHM, first.

### Different micro controllers
It is planned to use a different micro controller in the future for example to get more PWM channels and also more usable interrupt channels to measure the actual fan speeds in RPM. The schematics and PCB layout will only be for this new microcontroller.