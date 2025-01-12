# *mspot*

As in *mspot is less*, an M17 only repeater (or hot-spot) using an MMDVM modem

## Description

This open-source project will build an M17-only repeater or hot-spot for amateur radio. The design goals for this project include:
- **Easy to build**: Because the *mspot* repeater is a single application and is complete. It includes a novel M17 gateway that can connect to both M17 and URF reflectors.
- **Easy to configure**: With a single, self contained application, there is just a single configuration file, and the project builds an additional program, *inicheck*, that will intelligently analyze your ini file and reports any problems it finds.
- **Easy to use**: *mspot* includes many RF-base commands to manage its connection state and responds to commands with voice messages, making *mspot* ideal for a mobile, smart-phone tethered repeater.
- **Minimum executable size**: *mspot* is tiny, especially when you compare it to the multi-mode alternatives.
- **Excellent reliability**: You'll be the judge of that. Try it and find out.

It should support most any MMDVM-type modem because a significant portion of the project is based on a stripped-down version of G4KLX Jonathan Naylor's [MMDVMHost](https://github.com/g4klx/MMDVMHost). Unlike MMDVMHost, this project does not support any display device. It also does not support file locking, transparent port, or remote control.

This project will build *mspot*, a single executable program that has a built-in M17 gateway that will connect to both M17 reflectors and URF reflectors. The gateway portion of *mspot* is unique. It doesn't use the popular *time-slice* method characterized by a `clock()` routine, but rather uses a kind of *pass the baton* control scheme.

## Building *mspot*

These instructions are for Debian-based operating system.

After making sure your system is up to date: `sudo apt update && sudo apt upgrade`


There are a few things you need: `sudo apt install git build-essential nlohmann-json3-dev`


Then copy this repo: `git clone https://github.com/n7tae/mspot.git`

Within the *mspot* folder, copy a few files to this folder (don't forget the period at the end!): `cp config/{mspot.*,*.txt} ,`

Then you your favorite text editor to modify two of these files to your needs. Comments within these files will help you decide what you need to do:

- *mspot.mk* contain compile-time options.

You can then build *mspot*: `make`

## Configuring *mspot*

- *mspot.ini* sets run-time options.

You may have noticed that two programs were created at the build stage: *mspot* the M17-only repeater/hot-spot, and *inicheck*, a program that will check you initialization file for errors. *inicheck* tries to be very thorough. In addition to making sure that all needed variable are define and have reasonable values, it checks that path names are pointing to existing files on your system, and that those files are the kinds of files they need to be. Do: `./inicheck mspot.ini`. If any errors are identified, *mspot* won't run.

In the build section, a few `txt` files were copied. For connection information, *mspot* uses a host text file that is not compatible with the M17Hosts.txt file that is used by G4KLX's *M17Gateway* application. `M17_Hosts.txt` contains a list of all M17 and URF reflectors registered at [dvref.com](https://dvref.com/). This file contains a time-stamp so you know exactly when it was created. You can easily build an up-to-the-minute accurate host text file with the *make-m17-host-file* program that you can build using the [*ham-dht-tools*](https://github.com/n7tae/ham-dht-tools) repository. The second, `MyHost.txt` is a place for you to add other reflectors that aren't registered at dvref.com

## Starting *mspot*

To start *mspot*, go to its repo folder and type `./mspot mspot.ini`. Then `[Log] DisplayLevel` messages will be printed in the terminal window and if `[Log] FileLevel` is greater than zero, log messages will also be written to the folder and file specified in the ini file.

Alternatively, you can have *mspot* automatically launched on system boot up. To get started, copy the service script: `cp config/mspot.service .` and then edit the copy by changing the values within the `<>` brackets. Then do a `sudo make install`. You can do `sudo make uninstall` to stop this. Please note that installing and uninstalling require root privileges, but *mspot* can run with user privileges as long as the user is a member of the `dialout` group. In this case, you will want to enable `[Repeater] IsDaemon` and then be sure to specify the correct login id for `[Repeater] User`. If it's not starting correctly, look for the cause in the ini-specified log file and/or the systemd log: `sudo journalctl -u mspot`. If *mspot* launched successfully manually but won't install, more than likely you have a problem with your `mspot.service` file.

### Dialout

Do a `getent group dialout`. If the user is not listed, do a `sudo adduser <username> dialout`, where `<username>` is the login name of the user. After this, you can verify with another `getent` command.

## Using *mspot*

Make sure you have an M17 radio on and set to the proper frequency. You'll hear a startup message whenever it launches.

The following #destinations work:
- `ECHO` or `E` will parrot back anything that's transmitted to the repeater.
- `STATUS` or `I` will play a connection state message.
- You can connect to any known reflector module by putting that reflector's callsign and module letter in your radio and keying up. Be sure the reflector module letter is in the ninth position! For example `M17-QQQ A` or `URFQQQ  C`. Once you hear a message that it's connected successfully, the next time you key up, you'll be transmitting into the reflector module.
- `UNLINK` or `U` will disconnect you from a connected reflector module. Please note that *mspot* has to be in a disconnected state before you can connect to a reflector module.

There are two other commands that you might find useful:
- `RECORD` or `RECORD ?` , where `?` is a valid M17 character, will record and then play back a transmission. Without a valid ninth character, the recorder transmission will end up in the specified audio folder with the name *ECHO.dat* and this is exactly what `ECHO` or `E` does. `RECORD A` will end up in the specified audio folder with the name *alpha.dat*, `RECORD B` will end up in *bravo.dat, and so on. Likewise `RECORD 8` will end up in *eight.dat* and the three valid M17 punctuation characters will end up in obvious named files. Your recordings will remain until they are re-recorded, or they are manually removed.
- `PLAY` or `PLAY   ?` will play the message already recorded at that location, if it exists.

The echo and record functions will not record more than two minutes and the recording must be at least one second long.

### More about voice message

The robotic voice prompts currently being used are generated from a rather old open-source program, *espeak*. The first time you hear it, it might be a little difficult to understand. For example, the first time you hear espeak say "hotel", it may be hard to recognize. However, it gets much easier with repetition. However, if you are interested in recording your own voice messages or want to re-record messages in a different language, see the related [M17AudioTools](https://github.com/n7tae/M17AudioTools) repo. These are the tools used to create the *mspot* voice messages.

## Finally
