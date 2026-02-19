# *mspot*

An M17-only hot-spot for the M17 CC1200 Raspberry Pi hat.

## Description

This open-source project will build an M17-only hot-spot for amateur radio. The design goals for this project include:
- **Easy to configure**: With a single, self contained application, there is just a single configuration file, and the Makefile builds an additional program, *inicheck*, that will intelligently analyze your ini file and reports any problems it finds.
- **Easy to use**: *mspot* includes many RF-base commands to manage its connection state and responds to many commands with voice messages, making *mspot* ideal for a mobile, smart-phone tethered repeater. I hope it's also useful to sight-impaired hams.
- **Minimum executable size**: *mspot* is tiny, especially when you compare it to the multi-mode alternatives.
- **Excellent reliability**: You'll be the judge of that. Try it and find out.

This version of *mspot* only works with a Rasperry Pi running trixie OS and the [M17 CC1200 Raspberry Pi hat](https://m17project.org/cc1200-rpi-shield/). It is planned to also support the [M17 SX1255 Raspberry Pi hat](https://m17project.org/sx1255-rpi-shield/) in the future.

Having said that, it *might* be possible to get *mspot* to run on a different single board computer as long as that sbc has a Pi-compatible 40-bit GPIO header, and you can install or build and install a compatible `gpiod-dev` library. If you are interested in this, let us know, we can help.

Support for version 1.6 MMDVM modems is available in the `mmdvm` branch of this repo.

## First step, *trixie*

If you already have an SD card with *trixie* on it, you can probably use it but you'll want to read the `TRIXIE_SD_CARD_PREP.md` file for step-by-step instructions for how to make a *trixie* SD card for your Pi.

After you've done the `sudo apt update` and `sudo apt upgrade`, there are several packages you will need:

```
sudo apt install git build-essential cmake nlohmann-json3-dev libsqlite3-dev`
```

## The M17 library, libm17

This library does all the heavy lifting to convert between the symbols to/from the CC1200 and the *mspot* internet gateway.
1. In your home directory, clone the repo: `git clone https://github.com/M17-Project/libm17.git
2. Move to the repo: `cd libm17`
3. When you do this step, you'll see a warning about unit testing you can ignore. Prep the build: `cmake -B build`
4. Build it: `cmake --build build`
5. Install it: `sudo cmake --install build`
6. Your done! Return to your home directory: `cd`

## Dialout

If you created you trixie SD card using the instructions in the TRIXIE_SD_CARD_PREP.md file, you're all set because the user you defined is already a member of the *dialout* group! If not, read on...

The user that executes *mspot* needs to be in the `dialout` group! Do a `getent group dialout`. If the user is not listed, do a `sudo adduser <username> dialout`, where `<username>` is the login name of the user. After this, you can verify with another `getent grep dialout` command.

## Updating the CC1200 firmware

This version of *mspot* supports CC1200 firmware version 2.2. You don't need to compile the firmware, but you do need a few things. Here is how to install that version:
1. Install packages: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi`
2. Clone the firmware repo: `git clone https://github.com/M17-Project/CC1200_HAT-fw.git`
2. Move to the repo: `cd CC1200_HAT-fw/Release`
3. Flash it: `sudo stm32flash -v -R -i "-532&-533&532,533,:-532,-533,533" -w CC1200_HAT-fw.bin /dev/ttyAMA0`
4. Return to your home directory: `cd`

It will take several seconds to do the flashing and when it's all done, you'll see `Reset Done.` When you start *mspot* you'll see it report the CC1200 firmware version.

## Building *mspot*

First copy this repo: `git clone https://github.com/n7tae/mspot.git`

Move to the *mspot* folder: `cd mspot`, then copy a few configuration file to this folder (don't forget the period at the end!): `cp config/* .`

Then you can use your favorite text editor to modify these files to your needs. Comments within these files will help you decide what you need to do.

### Setting compile time options

Use your favorite editor to edit your new copy of *mspot.mk*.

If you are planning on automatically launching *mspot* when your Pi boots up, you should probably set both `DEBUG` and `USE_TS` to `false`. If you want to run *mspot* manually, then you should make sure `USE_TS` is `true`.

Once you're done editing this file, you can then build *mspot*: `make`

### Setting run-time options

Use your editor to edit your new copy of *mspot.ini*.

You may have noticed that two programs were created at the build stage: *mspot* the M17-only repeater/hot-spot, and *inicheck*, a program that will check you initialization file for errors. *inicheck* tries to be very thorough. In addition to making sure that all needed variable are define and have reasonable values, it checks that path names are pointing to existing files on your system, and that those files are the kinds of files they need to be. Do: `./inicheck mspot.ini`. If any errors are identified, *mspot* won't run.

Each parameter in the example ini file is commented to help you understand what it does. Using `inicheck` will tell you if your `UartPort` is pointing to a character device and it checks a lot of other things.

### Set up any custom destinations

In the `Building` section, a few `.txt` files were copied when you copied the contents of the *config* directory. These files are for connection information. `M17_Hosts.txt` contains a list of all M17 and URF reflectors registered at [dvref.com](https://dvref.com/) and contains a time-stamp so you know exactly when it was created. This file also contains additional information about the server if it is using the *Ham-DHT* network. The second, `MyHosts.txt` is a place for you to add other reflectors that aren't registered at dvref.com, or direct routing destinations you might want to use: See the `Direct Call` section below.

### Starting *mspot*

There are two options to starting mspot:

#### Manual start

To start *mspot* manually, go to its repo folder and type `./mspot mspot.ini`. Then log messages will be printed in the terminal window. Timestamp colors indicate where the message is being generated. Extra messages from the CC1200 modem class will be printed if the `[Modem]Debug` value is `true`. To stop *mspot*, just type \<Control>C. It will print a few messages as it shuts down.

#### It's always running

If you want to have *mspot* start whenever your system starts, edit your new copy of *mspot.service*. Everything in angle brackets, \<>, needs to be replaced with the proper value. If you want run *mspot* as root you can remove the definitions for `[Service]User` and `[Service]Group`. If not, you need to set these. Also make sure the `[Service]ExecStart` specifies the exact file path to your ini file and if you changed `BINDIR` in your *mspot.mk* file that will also have the full path to *mspot* on this same line. Once you have modified this mspot.service file, do `sudo make install` to install and start *mspot*, and `sudo make uninstall` to stop and uninstall it. Once running, there are lots of things you can do:
- To view *mspot*'s log in real time: `sudo journalctl -u mspot -f` Type \<Control>C to stop the view.
- If you want to know all the times you heard a particular callsign, like N0CALL: `sudo journalctl -u mspot | grep NOCALL` See `journalctl --help` for other options/features.
- To stop *mspot* without uninstalling it: `sudo systemctl stop mspot`. To start it back up: `sudo systemctl start mspot`. See `systemctl --help` for other features.
- For a quick look at how *mspot* is doing, including the last few lines of it's log: `systemctl status mspot`

## Connecting to a reflector

You can configure *mspot* to either automatically link to a reflector module when it starts, or you can manually link after it starts.

If you mostly connect to the same module on the same reflector, you can configure *mspot* to automatically connect to that reflector module when it starts up. In the `[Gateway]` section of your `mspot.ini` file, `StartupLink` is where you specify the reflector module to automatically line. If defined this link must be exactly 9 characters. Please note that there is ***one*** space between the M17 reflector designator and the module and ***two*** spaces between the URF designator and the module. The `MaintainLink` true/false parameter will control what your Gateway does of a link is lost. 

If you want more flexibility when *mspot* starts, don't configure a start-up. For example, if you typically visit more that one reflector during a session, it might be easier to manually link to a reflector, see the "Using *mspot*" section.

Finally a quick word about reflector. For legacy reflectors, *i.e.*, all URF reflectors and M17 reflectors with version less than 1.0.0, you have to use the destination as specified in this section to successfully transmit into the reflector. However, on later version of M17 reflectors, version greater than or equal to 1.0.0, you can use your radio's default BROADCAST destination address, or any other address and it will be forwarded to all clients on that module as it was set in your radio.

## Direct calls

Direct calls to another destination are supported, but in order for you to hear direct calls, you have to set up you home router to forward UDP port 17000 to the local network address of your Pi.

To place a direct call, you first need to setup the callsign and address of your destination in the MyHosts.txt file. After that, you put that callsign in the destination of your radio and key up once. You should see a message that your destination was found. The next time you key up, your voice packets will be routed to the specified IP address.

If you subsequently link to a reflector, you'll still possible hear incoming direct calls, if *mspot* is idle. If you want to place or return a direct call, you'll have to unlink from the reflector to which you are currently linked, and then set the proper destination in your radio and quick-key before you can reply.

## Using *mspot*

Make sure you have an M17 radio on and set to the proper frequency. You'll hear a startup message whenever it launches.

Controlling the gateway is done with your M17 radio by setting different *commands* in the destination of your radio and sending a short transmission to *mspot*.

You can connect to any known reflector module by putting that reflector's callsign and module letter in your radio and keying up. Be sure the reflector module letter is always at position nine! For example:

| Type | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|------|---|---|---|---|---|---|---|---|---|
| M17  | M | 1 | 7 | - | X | Y | Z |   | A |
| URF  | U | R | F | X | Y | Z |   |   | A |

There will alway be one space at position eight for an M17 reflector module destination, and two spaces at positions seven and eight for a URF reflector module destination.

Once you hear a message that it's connected successfully, the next time you key up, you'll be transmitting into the reflector module.
- **`ECHO`** or **`E`** will parrot back anything that's transmitted to the repeater. Maximum recording time is two minutes. Also, don't forget that non-legacy versions of the M17 reflector will echo back up to 20 seconds of your transmission if you change your destination to **`PARROT`**. You have to be already linked to the reflector.
- **`STATUS`** or **`S`** or **`I`** will play a connection state message.
- **`UNLINK`** or **`U`** will disconnect you from a connected reflector module. Please note that *mspot* has to be in a disconnected state before you can connect to a reflector module. If you try to change modules or change reflectors, your transmission will be ignored and a connection message will be played when you end your transmission.

There are two other commands that you might find useful:
- **`RECORD`** or **`RECORD ?`** , where **`?`** is any valid M17 character, will record and then play back a transmission. Without a valid ninth character, the recorder transmission will end up in the specified audio folder with the name *ECHO.dat* and this is exactly what **`ECHO`** or **`E`** does. **`RECORD A`** will end up in the specified audio folder with the name *alpha.dat*, **`RECORD B`** will end up in *bravo.dat*, and so on. Likewise **`RECORD 8`** will end up in *eight.dat* and the three valid M17 punctuation characters will end up in obvious named files. Your recordings will remain until they are re-recorded, or they are manually removed.
- **`PLAY`** or **`PLAY   ?`** will play the message already recorded at that location, if it exists.

The echo and record functions will not record more than two minutes and the recording must be at least one second long before it will be saved.

## Updating *mspot*

1. Go to the *mspot* repo directory and stop *mspot* **before** updating.
2. It is safest to do `make clean` but it's not absolutely necessary if you know what you are doing.
3. Do `git pull` in the *mspot* repo directory. If you see any `config/*` files being updated, that's a strong clue you that you will need to recopy those updated files to the repo base directory and possibly edit them. To be safe, move your old file before copying them down, for example `mv mspot.{ini,old}` will move `mspot.ini` to `mspot.old`. Then you can copy down the new config file, `cp config/mspot.ini .` to the working directory. Then you can edit the new file.
4. After you have possibly edited your new mspot.mk file, do `make`.
5. After you've edited a new mspot.ini file do `./inicheck mspot.ini`
to make sure there are no errors in your ini file.
6. If you are starting *mspot* automatically and a new mspot.service file came down, with the "git pull", you'll probably need to create a new mspot.service file.
7. You are now ready to restart *mspot*.

### More about voice message

The robotic voice prompts currently being used are generated from a rather old open-source program, *espeak*. The first time you hear it, it might be a little difficult to understand. For example, the first time you hear espeak say "hotel", it may be hard to recognize. However, it gets much easier with repetition. If you are interested in recording your own voice messages or want to re-record messages in a different language, see the related [mat](https://github.com/n7tae/mat) repo, the *M17 Audio Tools*. These are the tools used to create the *mspot* voice messages.

## Packet mode

I don't have a radio that can send or receive PM transmissions yet. The code is there, but it hasn't yet been debugged! *Caveat emptor!*

## License

This software is published using the GNU GPU, Version 3. Please see the enclosed `LICENSE` file for details.

## Finally

There is still a lot of work to do before *mspot* will be really useful and catch up with Jim N1ADJ's excellent go-based m17-gateway.
I am working on:
- A fully functional dashboard.
- Packet mode. I am waiting for OpenRTX to release firmware for my CS7000-M17 radio.
- Systemd support. I have some things to fix in *mspot's* output before I am ready for that.
- Better voices and more voice prompts. See the new generation of voices available in [*mat*](https://github.com/n7tae/mat).

73 de N7TAE
