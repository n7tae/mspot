# Preparing a Trixie-based Raspberry Pi for *mspot*

## Create a trixie-lite SD card image

Use the [Raspberry Pi Imager](https://www.raspberrypi.com/software/) to create the trixie-lite *.img file that you will burn to your SD card. *Image* is available for Windows, MacOS and Linux. Don't use a bargin-bin card!
1. Plug the SD card into the machine that you installed the Imager program. It should at least a 16 GByte card.
2. On the first screen select your Raspberry pi model. Note that *mspot* probably won't work with a Zero W, you'll need at least a Zero 2 W. You'll be doing RF frame detection on the Pi and this uses floating point calculations. The 32-bit Zero may not be able to keep up.
3. For the OS, 64-bit trixie lite is the first selection once you click on the `Raspberry Pi Os (other)` selection in the first drop-down menu.
4. Select the SD card you are burning and click the `NEXT` button.

In the `Use OS Customisation?` pop-up, press the `EDIT SETTINGS` buttons to set up you trixie OS lite. First, in the "GENERAL" tab:
1. Set your hostname. This will be the name of your system on your local network. Don't use spaces or punctuation and don't use a hostname that already exists on your local network.
2. Set the user name. Don't use `pi`, but I do recommend you use all lower case letters. Since I always access systems on my local network using my linux laptop, I use the same user name from my laptop. And go ahead and set a password too. Don't get lazy here and use `pa55word` or `passw0rd`!
3. Set up you wifi network with its name and password. If your wifi network doesn't advertise itself, click the `Hidden` box. And be sure to set you wifi country.
4. Set you locale with your timezone and you keyboard layout.

In the "SERVICES" tab, be sure to turn on SSH. You can select the `Use password authentication`, but I highly recommend you use `Allow public key authentication only`. You can generate a new key or use an existing key. Just copy and paste the contents of the *.public file into the space provided.

Finally, finish up your choices on the `OPTIONS` tab, then press the `SAVE` button. On the previous pop-up click `YES`. Once the card is imaged, remove it from you computer and put it in your Pi and power it up. Wait a minute or so because it will boot up a couple of times before it's ready to accept a login.

## Finalizing trixie

Connect using your ssh client to port 22 at \<hostname>.local, or use your local network name if you've named you local network (this is unusual).

### Finalize Locale with *raspi-config*

1. Do `sudo raspi-config` and use the up/down arrow keys on your keyboard to select the `Localisation Options` sub-menu, the press \<TAB>, then \<ENTER> on your keyboard.
2. This will bring up the Locale sub-menu with "L1 Locale" already selected. Press \<TAB>, then \<ENTER> again.
3. In a few seconds you will be presented a menu of locales. You can use your keyboard's up/down arrow keys to move and the \<Space> key to set or unset any selection. Local name begin with two lowercase letters that indicate the language, followed by an underscore, `_`, and two uppercase letters that indicate the country. You can unset the en_GB and select any other locale, select at least one, but make sure it is UTF-8. I use `en_US.UTF-8 UTF-8`.
4. Once you've selected your locale, press \<TAB>, then \<ENTER> on your keyboard. Then select your new locale and then \<TAB>, then \<ENTER>. Once that is done you can exit *raspi-config*.

### Edit boot files

Before you reboot into your chosen locale, you need to use your favorite linux editor, maybe *nano* or *vi* to edit two files, /boot/firmware/cmdline.txt and /boot/firmware/config.txt. Be sure to do this with root privileges and you might want to make a back-up of each of these file in case you make a mistake (`cp <filename> <backup>`):
1. Do `cd /boot/firmware`
2. Do `sudo <editor> cmdline.txt` and remove the part that begins with `console=serial0,115200 ` and then write out the file and exit the editor.
3. Do `sudo <editor> config.txt` and at the end of the file, in the "[all]" section, add:

```
dtoverlay=miniuart-bt
enable_uart=1
```
4. Then write out the file and exit the editor.

5. If the CC1200 is already mounted, you can now reboot the pi: `sudo shutdown -r now`, if it is not, do a `sudo shutdown -h now` and unplug you Pi and mount the CC1200. Once it reboots and you are logged in again, update trixie (and do this every week or so from now on):

```
sudo apt update 
# and if there is something available to upgrade, do:
sudo apt upgrade
```

You are now ready to install *mspot*!

## Trouble?

If you can't seem to log into you Pi after it first boots up, see if you can see your pi on your home router dashboard. If you do you can ssh directly to its reported IP address. Otherwise, you can unplug the power and pop the SD card back into you computer. Most computers will auto-mount both SD card partitions. Go the the BOOT partition and make sure the two files that you edited look okay. In the other partition, check /etc/hostfile for the proper hostname. Also, see if /home/\<usr> is there and if it is, is your *.public key file in the `.ssh` directory. If it's something you can fix with an editor, great, if not, you probably need to start over and redo your SD card again. Or you can plug in an hdmi monitor (you will need the correct cable and/or adaptor) and keyboard (w/ the correct cable/adaptor) and use it that way.

Also, and not recommended, you can install the full version of trixie and run everything from the OS gui.
