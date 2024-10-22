# Copyright (c) 2022 by Thomas A. Early N7TAE

# BASEDIR includes two places:
# $(BASEDIR)/bin for the executable
# $(BASEDIR)/share/locale for the language file(s)
BINDIR = /usr/local/bin

# By default, m17host uses the Ham-DHT network, a "distributed hash table" network.
# The Ham-DHT will provide addtional information about reflectors, making it easier to use them.
# if you don't want this, set this to false.
USE_DHT = true
