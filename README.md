<img src="artwork/openlager.png" alt="openlager" width="200" height="200" align="left"/>
STM32F4 based logging dongle for **HIGH RATE** logging

This project is inspired by [OpenLog](https://github.com/sparkfun/OpenLog).

*Why do something new?*  To attain higher rates!  The **openlager** hardware can accept data at high rates using slave SPI or fast async serial.  It uses a 4-wide SDIO interface to the card to support higher peak logging rates.  And the reasonable amount of memory buffer on the STM32F411 lets us ride out times when the SD card is busy erasing or doing other housekeeping without losing data.

The **openlager** hardware design and firmware are produced by the [dRonin](http://dronin.org) project, which produces high-quality flight controller software for drone racing and autonomous flight.  To log everything that's happening in a lossless fashion, high rate logs are required.  But we anticipate it being useful for a wide variety of applications.

# Licenses

The **openlager** hardware in `hardware/` is licensed under a Creative Commons Attribution 4.0 International License (CC BY 4.0), as contained in `hardware/LICENSE.txt`

The **openlager** firmware in `src/` is licensed under a simplified BSD license as found in `src/LICENSE.txt`

The **fatfs** library in `libs/fatfs` is licensed under the one-clause BSD license as found in the source files in that directory.

The **STM32F4xx_StdPeriph_Driver** library in `libs/STM32F4xx_StdPeriph_Driver` is largely licensed under a restrictive license that limits its usage to ST family processors.  To use this software on non-ST parts, the dependency on this library code must be removed.  Portions of this code (the `core_cm*` include headers) are licensed under a permissive 3 clause BSD license from ARM, as noted in the individual files.
