# Copyright (c) 2026 by Thomas A. Early N7TAE

# SYSTEMDDIR defines where the service will be installed.
SYSTEMDDIR = $(HOME)/.config/systemd/user/

# If you are going to run mspot from the command line, set USE_TS to true.
USE_TS = false

# DEBUG is for software debugging support.
# if mspot is crashing or locking up, set this to true and run it manually.
# If it is crashing, do "ulimit -c unlimited" before starting "./mspot mspot.ini",
# then when it crashes, you system will create a core dump file that can be examined with gdb.
# If it is locking up, set this to true and then launch with "gdb mspot", then when
# gdb gives you a prompt, type "run mspot.ini", then when it locks up, you can do a <Control>C
# and then use gdb commands to view the current state of each mspot thread.
DEBUG = false
