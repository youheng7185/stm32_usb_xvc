# XilinxVirtualCable on STM32 using USBFS

it's using modified virtual com usb stack to use bulk transfer between PC and STM32

## How to use

* clone repo, open the project in cubeide and compile

* if cubemx overwrite the modified usb stack library, use git checkout to restore them

* check the jtag pinout from ioc file, connect them to the fpga

* tested on xc7a35t board

## Reference

* https://github.com/kholia/xvc-pico
* https://github.com/Xilinx/XilinxVirtualCable
