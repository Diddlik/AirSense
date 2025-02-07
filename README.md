![](/images/CO2_SenseAir_S8.png)  
Carbon dioxide has a very negative effect on the human body and is a carcinogen. Being in a room with a high concentration of CO2 can cause weakness, drowsiness, headaches, and problems concentrating. In this regard, it is necessary to monitor the level of CO2 and take measures to reduce it.

Today there are several options available for CO2 measurement, the most interesting being the SenseAir S8. His readings will be displayed on the Zigbee network.
### How to compile
Follow this article https://zigdevwiki.github.io/Begin/IAR_install/

### Diagram
![](/images/Schematic_CO2_SenseAir_S8.png)  

The Zigbee part is implemented on the E18-MS1PA1-PCB module, besides it, the board contains the SenseAir S8 CO2 sensor itself  
![](/images/CO2_SenseAir_S8_2.png)   
![](/images/CO2_SenseAir_S8_1.png)   
and two variants of temperature sensors is the DS18B20 and the more universal BME280 sensor, which allows you to measure temperature, humidity and atmospheric pressure.

### PCB
The board is designed in the popular "USB stick" form factor, you can unsolder both micro USB and USB-A connectors   
![](/images/CO2_SenseAir_S8_6.png)
![](/images/CO2_SenseAir_S8_5.png)


The assembly of the device should not cause difficulties even for people with initial soldering skills, all elements are large enough, except for the BME280.

When assembled, the device looks like this, please note that a gap must be left between the SenseAir S8 and the board.  
![](/images/2020-09-25_14-17-18.png)

Anonymass wrote the firmware for this device, it is open source.  

### Support
The sensor is supported in the zigbee2mqtt via interval and external converter. It looks like this  
![](/images/CO2_SenseAir_S8_10.png)  
![](/images/CO2_SenseAir_S8_11.png)

also implemented support in SLS Gateway
![](/images/CO2_SenseAir_S8_15.png)  

According to the test results, the high sensitivity of SenseAir S8 was found, the sensor quickly responds to changes in the CO2 level.

Jager's daily schedule.  
![](/images/CO2_SenseAir_S8_8.png)

Anonymass' daily schedule.  
![](/images/CO2_SenseAir_S8_12.png)

### Other info
* The MHZ19B sensor (and clones) also can be installed on the board.
* End devices use sleep mode, so E18 board stays cold and BME280 shows more truthful values.

### How to join
 * Reset to FN rebooting device 5 times with interval less than 10 seconds, led will start flashing during reset  
 * Reset to FN by pressing and holding the button (1) 5 seconds

### User interface
#### LEDs
* LED1 blinks when accessing the CO2 sensor.
* LED2 blinks if the CO2 value is higher than the first set point (1000), while LED3 is off.
* LED3 blinks if the CO2 value is higher than the second set point (2000), while LED2 is on.

#### Buttons
* SW2 E18 Manual report / FN reset
* SW1 CO2 Calibration

### Files to reproduce
* [Gerbers and BOM](https://github.com/diyruz/AirSense/tree/master/hardware) by [Jager](https://t.me/Jager_f)  
* [Firmware](https://github.com/diyruz/AirSense/releases) by [@anonymass](https://t.me/anonymass)  
* [Housing](https://www.thingiverse.com/thing:4739711) by [ArtBrayko](https://www.thingiverse.com/ArtBrayko)  

[Original post by Jager](https://modkam.ru/?p=1715)  
