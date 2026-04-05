# Copyright (c) 2026 by Thomas A. Early N7TAE

# BINDIR defines where the binary will be installed.
# If you change this and running mspot using mspot.service, you'll need to update your mspot.service file.
BINDIR = /usr/local/bin

# WWWDIR defines where the dashboard index.php file will be installed.
# If you change this, you'll need to also change the working directory for the php miniserver
# in the mdash.service file
WWWDIR = /usr/local/www

# If you want mspot to print the timestamp with each message, set USE_TS to true.
# For example, if you are planning to run mspot from the command line, set USE_TS to true.
USE_TS = false

# Do you want to find IP addresses for URF and M17 reflectors using the M17Host.json file
# from hostfiles.refcheck.radio? Reflectors are registered at DVRef.com.
USE_DVREF = false

# If want to IP address (and other important info) for URF and M17 reflectors to come from
# the Ham-DHT, then make sure you have installed the OpenDht library, and set this to true.
USE_DHT = false

# DEBUG is for software debugging support.
# if mspot is crashing or locking up, set this to true and run it manually.
# If it is crashing, do "ulimit -c unlimited" before starting "./mspot mspot.ini",
# then when it crashes, you system will create a core dump file that can be examined with gdb.
# If it is locking up, set this to true and then launch with "gdb mspot", then when
# gdb gives you a prompt, type "run mspot.ini", then when it locks up, you can do a <Control>C
# and then use gdb commands to view the current state of each mspot thread.
DEBUG = false
