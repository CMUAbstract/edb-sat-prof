export BOARD ?= edb

TOOLCHAINS = \
	gcc \

include ext/maker/Makefile

# Paths to toolchains here if not in or different from defaults in Makefile.env
