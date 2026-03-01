# Picomimi Makefile
# For RP2040/RP2350 ARM Cortex-M0+/M33

# Toolchain
CC = arm-none-eabi-gcc
AS = arm-none-eabi-as
LD = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

# Target
TARGET = picomimi

# Directories
BUILD = build
SRC_DIRS = core sched mm shell drivers

# Flags
CFLAGS = -mcpu=cortex-m0plus -mthumb -Os -g
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -Iinclude
CFLAGS += -DPICOMIMI_RP2040

LDFLAGS = -T picomimi.ld
LDFLAGS += -nostdlib -nostartfiles
LDFLAGS += --gc-sections
LDFLAGS += -Map=$(BUILD)/$(TARGET).map

# Sources
SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(patsubst %.c,$(BUILD)/%.o,$(SRCS))

# Rules
all: $(BUILD)/$(TARGET).uf2

$(BUILD)/$(TARGET).uf2: $(BUILD)/$(TARGET).bin
	@echo "  UF2   $@"
	@# Would use uf2conv here
	@cp $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@echo "  BIN   $@"
	@$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $^
	@$(SIZE) $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)

flash: $(BUILD)/$(TARGET).uf2
	@echo "Copy $(TARGET).uf2 to RPI-RP2 drive"
	@# Or use picotool: picotool load $(BUILD)/$(TARGET).uf2

.PHONY: all clean flash

# Dependencies
-include $(OBJS:.o=.d)
