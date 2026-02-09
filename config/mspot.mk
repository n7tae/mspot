# Copyright (c) 2022 by Thomas A. Early N7TAE

# BINDIR defines where the binary will be installed.
# If you change this and running mspot using mspot.service, you'll need to update your mspot.service file.
BINDIR = /usr/local/bin

# If you don't want mspot to print the timestamp with each message, set USE_TS to false.
# For example, if you are planning to use systemd to run mspot, set USE_TS to false.
USE_TS = true

# DEBUG is for software debugging support.
# if mspot is crashing or locking up, set this to true and run it manually.
# If it is crashing, do "ulimit -c unlimited" before starting "./mspot mspot.ini",
# then when it crashes, you system will create a core dump file that can be examined with gdb.
# If it is locking up, set this to true and then launch with "gdb mspot", then when
# gdb gives you a prompt, type "run mspot.ini", then when it locks up, you can do a <Control>C
# and then use gdb commands to view the current state of each mspot thread.
DEBUG = false
