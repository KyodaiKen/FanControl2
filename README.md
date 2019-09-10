# FanControl2
This is a DIY project that has also a PCB circuit and design on EasyEDA. It will be published later as I have to redesign it.
## Description
This project is for water cooling enthusiast. Be it custom loop or AiO, the fan control of motherboard leave open a lot to be desired.

For example, when the system is busy, the software of the motherboard manufacturers might stall and your fan speed is not being updated anymore which can lead to overheating issues especially when overclocking.

## What can this piece of firmware do for me?
You can now program curves via COM interface into it.

You open a com console with it and say
`sc <curve 0...2> <x y x y x y x y ... space separated>` to program a curve.
*For example*
```
sc 0 0 0 1 15 2 20 3 35 99 35
```
The first 0 is the curve ID, more on that below. The next numbers separated by spaces are `x y x y x y` representations of the curve coordinates. X is the temperature, y is the fan speed.

Each curve is for a sensor. Curve 0 is water temperature over ambient for the rad fans, curve 1 is reserved for another temperature sensor and fan header and curve 2 is ambient and case fans. Points are linearily interpolated inbetween, so when you have an off temperature the fan speed will be extrapolated between the 2 nearest points in a linear fashion. If you want to change how it works, you can change the code and flash it again!

`gc <curve>` lists the points of your curve you've programmed.

You can program up to 168 curve points x and y which gives you 84 xy coordinates for temp (x) and fan speed (y) per curve!
This number resulted from the EEPROM in Arduino Nano having a whopping 512 bytes of storage.

Send `gs` and you get sensor readings and fan speeds:

```
22.0 20.3
!NAN 100.0
20.0 23.9
```

First number is temperature, second is fan speed in percent! I will get a different controller board, make a new PCB and stuff to also measure the fan speed in RPM soon! One each channel.

Send `psc` and it goes into a mode where it spits out the same sensor data as above every second and you can stop this mode with !psc.

## Error messages
* `e0` - The curve you want to store does not exist. Enter a number between 0 and 2.
* `e1` - Curve points are not even, you need 2 pairs of values for x and y data points.
* `e2` - The number of curve points exceeds the maximum (84) that the EEPROM can store for each curve.
* `EEPROM BAD` - When you use a new controller and flash this firmware, there could be garbage in it's EEPROM and the data plausability check detected it. Simple default curves were loaded and you are prompted to send your custom ones.

## UI for your PC
It's planned but not yet finished.
You can use PuTTy or other terminal programs to send and receive commands via the Arduino's COM interface. The speed is 9600 baud and the rest is default.