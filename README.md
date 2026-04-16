# Firmware for Resistance Measurement board

## Building
The project is designed for MCUXpresso v24.12.148. Other versions will likely work, but may require project setting changes. MCUXpresso (as well as older versions) can be downloaded from [NXP](https://www.nxp.com/design/design-center/software/development-software/mcuxpresso-software-and-tools-/mcuxpresso-integrated-development-environment-ide:MCUXpresso-IDE)

An SDK for the LPC5514 is necessary for the project, and should be installed in MCUXpresso. The default SDK (from the [SDK Builder](https://mcuxpresso.nxp.com/) website) was used.

## FreeRTOS
The NXP SDK contains an older version of FreeRTOS, and is not used in this project. Mainline FreeRTOS is included as a git submodule to make updates easier. Clone the directory with `git clone --recurse-submodules <repository>` can be used to pull both this repository and the FreeRTOS code (needed to build the project). Alternatively, if you've already cloned this project, you can also do `git submodule update --init --recursive` to pull the FreeRTOS code.

