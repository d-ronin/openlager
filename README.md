<img src="artwork/openlager.png" alt="openlager" width="200" height="200" align="left"/>
STM32F4 based logging dongle for **HIGH RATE** logging

This project is inspired by [OpenLog](https://github.com/sparkfun/OpenLog).

*Why do something new?*  To attain higher rates!  The **openlager** hardware can accept data at high rates using slave SPI or fast async serial.  It uses a 4-wide SDIO interface to the card to support higher peak logging rates.  And the reasonable amount of memory buffer on the STM32F411 lets us ride out times when the SD card is busy erasing or doing other housekeeping without losing data.

The openlager hardware design and firmware are produced by the [dRonin](http://dronin.org) project, which produces high-quality flight controller software for drone racing and autonomous flight.  To log everything in a lossy fashion, high rate logs are required.  But we anticipate it being useful for a wide variety of applications.
