# MORe

As is *less is MORe*, an M17 only repeater (or hot-spot) using an MMDVM modem

## Description

This open-source project will build an M17-only repeater or hot-spot for amateur radio. It should support most any MMDVM=type modem because a significant portion of the project is based on a stripped-down version of G4KLX Jonathan Naylor's [MMDVMHost](https://github.com/g4klx/MMDVMHost). Unlike MMDVMHost, this project does not support any display device. It also does not support file locking, transparent port, or remote control. The design goals for this project include easy to build, easy to configure, easy to use, minimum executable size, and excellent reliability.

This project will build *morhs*, a single executable program that has a built-in M17 gateway that will connect to both M17 reflectors and URF reflectors. For connection information, *morhs* uses a host text file that is not compatible with the M17Hosts.txt file that comes with G4KLX's M17Gateway application. A version of the required file is included in the config folder. You can easily build an up-to-the-minute accurate host text file with the *make-m17-host-file* program that you can build using the [*ham-dht-tools*](https://github.com/n7tae/ham-dht-tools) repository.

This project will also build *inicheck*, a program that will check you initialization file for errors. *inicheck* tries to be very thorough. It checks that file path names point to an existing file, and that the device path pointing to your MMDVM modem is there and looks like a character device.
