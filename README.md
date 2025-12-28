# *mspot*

An M17-only hot-spot for the M17 CC1200 Raspberry Pi hat.

## Description

This open-source project will build an M17-only hot-spot for amateur radio. The design goals for this project include:
- **Easy to build**: Because the *mspot* repeater is a single application and is complete. It includes a novel M17 gateway that can connect to both M17 and URF reflectors.
- **Easy to configure**: With a single, self contained application, there is just a single configuration file, and the project builds an additional program, *inicheck*, that will intelligently analyze your ini file and reports any problems it finds.
- **Easy to use**: *mspot* includes many RF-base commands to manage its connection state and responds to commands with voice messages, making *mspot* ideal for a mobile, smart-phone tethered repeater. I am hoping it is also useful to sight-impaired hams.
- **Minimum executable size**: *mspot* is tiny, especially when you compare it to the multi-mode alternatives.
- **Excellent reliability**: You'll be the judge of that. Try it and find out.

This version of *mspot* only works with the [M17 CC1200 Raspberry Pi hat](https://m17project.org/cc1200-rpi-shield/). It is planned to also support the [M17 SX1255 Raspberry Pi hat](https://m17project.org/sx1255-rpi-shield/) in the future. 

## Building *mspot*

These instructions are for Debian-based operating system.

After making sure your system is up to date: `sudo apt update && sudo apt upgrade`


There are a few things you need: `sudo apt install git build-essential nlohmann-json3-dev`


Then copy this repo: `git clone https://github.com/n7tae/mspot.git`

Within the *mspot* folder, copy a few files to this folder (don't forget the period at the end!): `cp config/{mspot.*,*.txt} .`

Then you can use your favorite text editor to modify two of these files to your needs. Comments within these files will help you decide what you need to do:

- *mspot.mk* contain compile-time options.

Once you edit this file, you can then build *mspot*: `make`

## Configuring *mspot*

- *mspot.ini* sets run-time options.

You may have noticed that two programs were created at the build stage: *mspot* the M17-only repeater/hot-spot, and *inicheck*, a program that will check you initialization file for errors. *inicheck* tries to be very thorough. In addition to making sure that all needed variable are define and have reasonable values, it checks that path names are pointing to existing files on your system, and that those files are the kinds of files they need to be. Do: `./inicheck mspot.ini`. If any errors are identified, *mspot* won't run.

Each parameter in the example inifile is commented to help you understand what it does. Using `inicheck` will at least tell you if your `UartPort` is pointing to a character device.

In the `Building` section, a few `.txt` files were copied. These files are for connection information. `M17_Hosts.txt` contains a list of all M17 and URF reflectors registered at [dvref.com](https://dvref.com/) and contains a time-stamp so you know exactly when it was created. You can build an up-to-the-minute accurate host text file with the *make-m17-host-file* program that is part of the [*ham-dht-tools*](https://github.com/n7tae/ham-dht-tools) repository. The second, `MyHosts.txt` is a place for you to add other reflectors that aren't registered at dvref.com.

### Dialout

The user that executes *mspot* needs to be in the `dialout` group! Do a `getent group dialout`. If the user is not listed, do a `sudo adduser <username> dialout`, where `<username>` is the login name of the user. After this, you can verify with another `getent` command.

## Starting *mspot*

To start *mspot*, go to its repo folder and type `./mspot mspot.ini`. Then `[Log] DisplayLevel` messages will be printed in the terminal window.

Alternatively, you can have *mspot* automatically launched on system boot up. To get started, copy the service script: `cp config/mspot.service .` and then edit the copy by changing the values within the `<>` brackets. Then do a `sudo make install`. You can do `sudo make uninstall` to stop this. Please note that installing and uninstalling require root privileges, but *mspot* can run with user privileges as long as the user is a member of the `dialout` group. In this case, you will want to enable `[Repeater] IsDaemon` and then be sure to specify the correct login id for `[Repeater] User`. If it's not starting correctly, look for the cause in the ini-specified log file and/or the systemd log: `sudo journalctl -u mspot`. If *mspot* launched successfully manually but won't install, more than likely you have a problem with your `mspot.service` file.

## Connecting to a reflector

You can confgure *mspot* to either automatically link to a reflector module when it starts, or you can manually link after it starts.

If you mostly connect to the same module on the same reflector, you can configure *mspot* to automatically connect to that reflector module when it starts up. In the `[Gateway]` section of your `mspot.ini` file, `StartupLine` is where you specify the reflector module to automatically line. Please note that there is ***one*** space between the M17 reflector designator and the module and ***two*** spaces between the URF designator and the module. The `MaintainLink` true/false parameter will control what your Gateway does of a link is lost. If you want more flexibility when *mspot` starts, don't configure a start-up.

To link to a reflector manually, set `M17-xxx y` or `URFxxx  y` into the DST of your M17 radio and quick-key to connect to, or transmit into a reflector. Here `xxx` if the reflector's designator, 3 uppercase letters, `A`-`Z` and or decimal digits `0`-'`9` and `y` is the module, an uppercase letter. If your system is not currently linked a quick key-up with this in your RADIO SRC callsign will, if found in either the files specified in the `HostPath` or the `MyHostPath` parameters in the `Gateway` section of your ini file.

If you are not currently connected to another reflector, *mspot* will attempt to link to your chosen reflector module when you release the PTT on your M17 radio. If it successfully links, you'll hear a confirmation message, otherwise, you'll here a failure message. *Mspot* must be unlinked before it can be linked. If you are already connected to a reflector module, and you change the destination to another module or another reflector, your transmission won't go anywhere. After you release the PTT key, you will hear an "Already connected to ..." message.

Finally a quick word about reflector. For legacy reflectors, *i.e.*, all URF reflectors and M17 reflectors with version less than 1.0.0, you have to use the destination as specified in this section to successfully transmit into the reflector. However, on later version of M17 reflectors, version greater than or equal to 1.0.0, you can use your radio's default BROADCAST destination address, or any other address and it will be forwarded to all clients on that module as it was set. 

## Using *mspot*

Make sure you have an M17 radio on and set to the proper frequency. You'll hear a startup message whenever it launches.

Controlling the gateway is done with your M17 radio by setting different *commands* in the destination of your radio and sending a short transmission to *mspot*.

The following #destinations work:
- You can connect to any known reflector module by putting that reflector's callsign and module letter in your radio and keying up. Be sure the reflector module letter is in the ninth position! For example `M17-QQQ A` or `URFQQQ  C`. Once you hear a message that it's connected successfully, the next time you key up, you'll be transmitting into the reflector module. For more details, see the previous section.
- `ECHO` or `E` will parrot back anything that's transmitted to the repeater. Maximum recording time is two minutes. Also, don't forget that non-legacy versions of the M17 reflector will echo back up to 20 seconds of your transmission if you change your destination to `PARROT` or `#PARROT`. You have to be already linked to the reflector.
- `STATUS` or `I` will play a connection state message.
- `UNLINK` or `U` will disconnect you from a connected reflector module. Please note that *mspot* has to be in a disconnected state before you can connect to a reflector module. If you try to change modules or change reflectors, your transmission will be ignored and a connection message will be played when you end your transmission.

There are two other commands that you might find useful:
- `RECORD` or `RECORD ?` , where `?` is any valid M17 character, will record and then play back a transmission. Without a valid ninth character, the recorder transmission will end up in the specified audio folder with the name *ECHO.dat* and this is exactly what `ECHO` or `E` does. `RECORD A` will end up in the specified audio folder with the name *alpha.dat*, `RECORD B` will end up in *bravo.dat*, and so on. Likewise `RECORD 8` will end up in *eight.dat* and the three valid M17 punctuation characters will end up in obvious named files. Your recordings will remain until they are re-recorded, or they are manually removed.
- `PLAY` or `PLAY   ?` will play the message already recorded at that location, if it exists.

The echo and record functions will not record more than two minutes and the recording must be at least one second long.

### More about voice message

The robotic voice prompts currently being used are generated from a rather old open-source program, *espeak*. The first time you hear it, it might be a little difficult to understand. For example, the first time you hear espeak say "hotel", it may be hard to recognize. However, it gets much easier with repetition. However, if you are interested in recording your own voice messages or want to re-record messages in a different language, see the related [M17AudioTools](https://github.com/n7tae/mat) repo. These are the tools used to create the *mspot* voice messages.

The author would be interested in finding a better sounding open-source voice, but so far, all that have been evaluated fall short for one reason or another, the two most common reason they are rejected is either the S/N is too low and automatic word detected, used to build the M17 alphabet database, does not work, or, when encoded to Codec2 and then decoded, the decoded audio quality is inferior.

## License

This software is published using the GNU GPU, Version 2. Please see the enclosed `LICENSE` file for details.

## Finally

There is still a lot of work to do before it will be really useful to my target audience.

73 de N7TAE
