export BOARD ?= sprite-edb-v1.2

TOOLCHAINS = \
	gcc \

include ext/maker/Makefile

# Paths to toolchains here if not in or different from defaults in Makefile.env
