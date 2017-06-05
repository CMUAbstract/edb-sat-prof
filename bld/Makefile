EXEC = edbsat

OBJECTS = \
	main.o \
	payload.o \
	profile.o \
	flash.o \
	bits.o \
	power.o \
	random.o \

DEPS += \
	libio \
	libmspsoftuart \
	libmsp \
	libedbserver \
	libcapybara \
	libsprite \
	libmspuartlink \

# Configuration local to this app executable
export CONFIG_WATCHDOG = 1
export CONFIG_RADIO_TRANSMIT_PAYLOAD = 1
export CONFIG_COLLECT_ENERGY_PROFILE = 1
export CONFIG_SEED_RNG_FROM_VCAP = 1
export CONFIG_ENERGY_PROFILE_MIN_VOLTAGE = 3276
export CONFIG_PROFILE_SUB_BYTE_BUCKET_SIZES = 0

export WATCHDOG_CLOCK = ACLK
export WATCHDOG_INTERVAL = 8192K # 4 minutes

export PIN_APP_SW = 3.7 # APP power rail switch
export PIN_VBANK = 2.4 # divided Vbank voltage
export COMP_CHAN_VBANK = B.4 # comparator channel for Vbank voltage
export VBANK_DIV = 100:750
export VBANK_COMP_SETTLE_MS = 2 # time (ms) before reading comparator

# Can't get 8MHz exactly with 32768Hz DCOREF, closest is 7995392Hz or 8028160HzHz
export MAIN_CLOCK_FREQ = 8028160

export CLOCK_FREQ_ACLK = 32768 # Hz
export CLOCK_FREQ_SMCLK = $(MAIN_CLOCK_FREQ)

# Setpoint of voltage supply to EDB MCU (set by resistor divider in hardware)
export VDD_EDB = 2.4 # V
# Stop profiling when supercap voltage drops below this threshold
export PROFILING_VBANK_MIN = 2.0 # V

# Stop profiling after this interval elapses
export PROFILING_TIMEOUT_MS = 16000

# Configuration for libedbserver
export CONFIG_ENABLE_WATCHPOINTS = 1
export CONFIG_ENABLE_WATCHPOINT_CALLBACK = 1
export CONFIG_TARGET_UART = 0
export CONFIG_HOST_UART = 0
export CONFIG_ENABLE_DEBUG_MODE = 1
export CONFIG_PULL_DOWN_ON_COMM_LINES = 1

export CONFIG_ENABLE_DEBUG_MODE_TIMEOUTS = 1
export CONFIG_ENERGY_BREAKPOINTS = 0
export CONFIG_CHARGE_CMDS = 0

# TEMPORARY: for debugging
export CONFIG_STATE_PINS = 0
export CONFIG_EVENT_PINS = 1

export LIBMSP_CLOCK_SOURCE = DCO
export LIBMSP_DCO_REF_SOURCE = REFO
export LIBMSP_DCO_REF_CLOCK_DIV = 1
export LIBMSP_DCO_FREQ = $(MAIN_CLOCK_FREQ)
export LIBMSP_CORE_VOLTAGE_LEVEL = 2

export LIBMSP_SLEEP_TIMER = A.1.0
export LIBMSP_SLEEP_TIMER_CLK = ACLK
export LIBMSP_SLEEP_TIMER_DIV = 8*2

export LIBIO_BACKEND = swuart
export LIBMSPSOFTUART_PORT_TX = 3.1
export LIBMSPSOFTUART_BAUDRATE = 9600
export LIBMSPSOFTUART_CLOCK_FREQ = $(MAIN_CLOCK_FREQ)
export LIBMSPSOFTUART_TIMER = A.0.0

export LIBMSPUARTLINK_PIN_RX = 1.5
export LIBMSPUARTLINK_UART_IDX = 0
export LIBMSPUARTLINK_BAUDRATE = 4800
export LIBMSPUARTLINK_CLK = ACLK

# TODO: Why the mismatch with LIBSPRITE_CLOCK_FREQ?
export LIBSPRITE_CLOCK_FREQ = 8000000

# Select a pair of indexes of PRN arrays for Gold code communication
export LIBSPRITE_PRN_0 = 266
export LIBSPRITE_PRN_1 = 267

# Second board
#export LIBSPRITE_PRN_0 = 268
#export LIBSPRITE_PRN_1 = 269

export LIBCAPYBARA_PIN_VBOOST_OK = 2.0
export LIBCAPYBARA_PIN_BOOST_SW = J.0

export FLASH_STORAGE_SEGMENT = 0x1900
export FLASH_STORAGE_SEGMENT_SIZE = 128
ERASE_SEGMENTS = $(FLASH_STORAGE_SEGMENT)

include ../Makefile.config

# Need headers only, hence not a usual dep
override CFLAGS += -I$(LIB_ROOT)/libedb/src/include
