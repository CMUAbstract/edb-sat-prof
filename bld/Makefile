EXEC = edbsat

OBJECTS = \
	main.o \
	payload.o \
	profile.o \

DEPS += \
	libmsp \
	libedbserver \

# Configuration local to this app executable
export CONFIG_RADIO_TRANSMIT_PAYLOAD = 0
export CONFIG_ENABLE_PAYLOAD = 1
export CONFIG_PERIODIC_PAYLOAD = 1
export CONFIG_COLLECT_ENERGY_PROFILE = 1
export CONFIG_SEND_PAYLOAD_TO_HOST = 0
export CONFIG_SLEEP_IN_MAIN_LOOP = 0
export CONFIG_SEED_RNG_FROM_VCAP = 1
export CONFIG_WATCHPOINT_COLLECTION_TIME = 0x1fff
export CONFIG_TARGET_COMM_TIMEOUT = 0xff
export CONFIG_ENERGY_PROFILE_MIN_VOLTAGE = 3276
export CONFIG_PROFILE_SUB_BYTE_BUCKET_SIZES = 0

# Configuration for libedbserver
export CONFIG_ENABLE_WATCHPOINTS = 1
export CONFIG_ENABLE_WATCHPOINT_CALLBACK = 1
export CONFIG_TARGET_UART = 1
export CONFIG_HOST_UART = 0
export CONFIG_ENABLE_DEBUG_MODE = 1
export CONFIG_PULL_DOWN_ON_COMM_LINES = 1

export CONFIG_ENABLE_PAYLOAD = 1
export CONFIG_COLLECT_ENERGY_PROFILE = 1
export CONFIG_COLLECT_APP_OUTPUT = 1
export CONFIG_AUTO_ENABLED_WATCHPOINTS = 4
export CONFIG_RADIO_TRANSMIT_PAYLOAD = 1
export CONFIG_TARGET_POWER_SWITCH = 0
export CONFIG_SLEEP_IN_MAIN_LOOP = 1
export CONFIG_WATCHDOG = 1
export CONFIG_TASK_DRIVEN = 1
export CONFIG_ENABLE_DEBUG_MODE_TIMEOUTS = 1

# TEMPORARY: for debugging
export CONFIG_STATE_PINS = 0
export CONFIG_EVENT_PINS = 1

export CONFIG_DEV_CONSOLE = 1

export LIBMSP_CLOCK_SOURCE = DCO
export LIBMSP_DCO_REF_SOURCE = REFO
export LIBMSP_DCO_REF_CLOCK_DIV = 1
export LIBMSP_DCO_FREQ = 8192000ull

export LIBMSPSOFTUART_PORT_TX = 3.1
export LIBMSPSOFTUART_BAUDRATE = 115200
export LIBMSPSOFTUART_CLOCK_FREQ = 8192000
export LIBMSPSOFTUART_TIMER = A.0.0

export LIBSPRITE_CLOCK_FREQ = 8000000

# Select a pair of indexes of PRN arrays for Gold code communication
# export LIBSPRITE_PRN_0 = 266
# export LIBSPRITE_PRN_1 = 267

# Second board
#export LIBSPRITE_PRN_0 = 268
#export LIBSPRITE_PRN_1 = 269

include ../Makefile.config

# To build the dependee that uses the headers, we need the config macros here
include $(LIB_ROOT)/libmsp/bld/Makefile.config
include $(LIB_ROOT)/libedbserver/bld/Makefile.config

# Need headers only, hence not a usual dep
override CFLAGS += -I$(LIB_ROOT)/libedb/src/include

CONFIG_PRINTF_LIB = libmspsoftuart
include $(MAKER_ROOT)/Makefile.console