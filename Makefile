TOOLS_DIR := tools
ARM_SDK_DIR := $(TOOLS_DIR)/gcc-arm-none-eabi-5_2-2015q4
ARM_SDK_PREFIX := $(ARM_SDK_DIR)/bin/arm-none-eabi-

ifeq ("$(wildcard $(ARM_SDK_PREFIX)gcc*)","")
    $(error **ERROR** ARM-SDK is not in $(ARM_SDK_DIR))
endif

INC :=
INC += inc
INC += libs/STM32F4xx_StdPeriph_Driver/inc
INC += libs/fatfs
INC += libs/inc

BUILD_DIR := build

STDPERIPH_SRC :=
STDPERIPH_SRC += stm32f4xx_adc.c
STDPERIPH_SRC += stm32f4xx_dma.c
STDPERIPH_SRC += stm32f4xx_flash.c
STDPERIPH_SRC += stm32f4xx_gpio.c
STDPERIPH_SRC += stm32f4xx_iwdg.c
STDPERIPH_SRC += stm32f4xx_pwr.c
STDPERIPH_SRC += stm32f4xx_rcc.c
STDPERIPH_SRC += stm32f4xx_sdio.c
STDPERIPH_SRC += stm32f4xx_spi.c
STDPERIPH_SRC += stm32f4xx_tim.c
STDPERIPH_SRC += stm32f4xx_usart.c
STDPERIPH_SRC += misc.c

STDPERIPH_SRC := $(patsubst %,libs/STM32F4xx_StdPeriph_Driver/src/%,$(STDPERIPH_SRC))
FATFS_SRC := $(wildcard libs/fatfs/*.c)
OTHERLIB_SRC := $(wildcard libs/src/*.c)
SHARED_SRC := $(wildcard shared/*.c) $(OTHERLIB_SRC) $(FATFS_SRC) $(STDPERIPH_SRC)
OPENLAGER_SRC := $(wildcard src/*.c)
OPENLAGER_LOADER_SRC := $(wildcard loader/*.c)

SRC := $(SHARED_SRC) $(OPENLAGER_SRC)
OBJ := $(patsubst %.c,build/%.o,$(SRC))

BOOTLOADER_SRC := $(SHARED_SRC) $(OPENLAGER_LOADER_SRC)
BOOTLOADER_OBJ := $(patsubst %.c,build/%.o,$(BOOTLOADER_SRC))

OBJ_FORCE :=

ifeq ("$(STACK_USAGE)","")
    CCACHE_BIN := $(shell which ccache 2>/dev/null)
endif

CC := $(CCACHE_BIN) $(ARM_SDK_PREFIX)gcc

CPPFLAGS += $(patsubst %,-I%,$(INC))
CPPFLAGS += -DSTM32F411xE -DUSE_STDPERIPH_DRIVER

CFLAGS :=
CFLAGS += -mcpu=cortex-m4 -mthumb -fdata-sections -ffunction-sections
CFLAGS += -fomit-frame-pointer -Wall -Os -g3

ifneq ("$(STACK_USAGE)","")
    OBJ_FORCE := FORCE
    CFLAGS += -fstack-usage

ALL: build/lager.stack build/bootlager.stack

else
    CFLAGS += -Werror
endif

CFLAGS += -DHSE_VALUE=8000000

LDFLAGS := -nostartfiles -Wl,-static -lc -lgcc -Wl,--warn-common
LDFLAGS += -Wl,--fatal-warnings -Wl,--gc-sections
LDFLAGS += -Tshared/stm32f411.ld

all: build/ef_lager.bin

flash: build/ef_lager.bin
	openocd -f /usr/local/share/openocd/scripts/interface/stlink-v2.cfg -f /usr/local/share/openocd/scripts/target/stm32f4x.cfg -f flash.cfg

build/ef_lager.bin: build/bootlager.bin build/lager.bin
	cat build/bootlager.bin build/lager.bin > $@

%.bin: %
	$(ARM_SDK_PREFIX)objcopy -O binary $< $@

build/lager.stack: $(OBJ) build/lager
	printf 'Memory used by data+BSS: %d\n\n' `/bin/echo -n '0x';$(ARM_SDK_PREFIX)nm build/lager | grep _ebss | cut -c 2-8` > $@
	./misc/avstack.pl $(OBJ) >> $@

build/bootlager.stack: $(BOOTLOADER_OBJ) build/bootlager
	printf 'Memory used by data+BSS: %d\n\n' `/bin/echo -n '0x';$(ARM_SDK_PREFIX)nm build/bootlager | grep _ebss | cut -c 2-8` > $@
	./misc/avstack.pl $(BOOTLOADER_OBJ) >> $@

build/lager: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ -Tsrc/memory.ld $(LDFLAGS)

build/bootlager: $(BOOTLOADER_OBJ)
	$(CC) $(CFLAGS) $(BOOTLOADER_OBJ) -o $@ -Tloader/memory.ld $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

build/%.o: %.c $(OBJ_FORCE)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

inc/%cfg.h: %.cfg FORCE
	echo "const " > $@
	xxd -i $< >> $@

FORCE:
