TARGET      := ClassiCube
BUILD_DIR   := build/kolibrios
SOURCE_DIRS := src src/kolibrios
C_SOURCES   := $(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.c))
C_OBJECTS   := $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
OBJECTS     := $(C_OBJECTS)

#---------------------------------------------------------------------------------
# Toolchain
#---------------------------------------------------------------------------------
CC          := kos32-gcc
LD          := kos32-ld
OBJCOPY     := kos32-objcopy

KOS32_SDK_DIR := /c/kolibri/contrib/sdk
export PATH := $(PATH):/home/autobuild/tools/win32/bin:/MinGW/bin

#---------------------------------------------------------------------------------
# Paths
#---------------------------------------------------------------------------------
INCLUDE_PATH := -I$(KOS32_SDK_DIR)/sources/newlib/libc/include
LIB_PATH     := -L$(KOS32_SDK_DIR)/lib -L/home/autobuild/tools/win32/mingw32/lib -L/home/autobuild/tools/win32/lib

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
CFLAGS  := -c -O2 -fomit-frame-pointer -fno-math-errno
CFLAGS  += -U__WIN32__ -U_Win32 -U_WIN32 -U__MINGW32__ -UWIN32
CFLAGS  += -DPLAT_KOLIBRIOS $(INCLUDE_PATH)

LD_FLAGS := -static -S -nostdlib -T$(KOS32_SDK_DIR)/sources/newlib/app.lds
LD_FLAGS += -Map $(BUILD_DIR)/$(TARGET).map --image-base 0

LIBS := -lgcc -lc.dll

#---------------------------------------------------------------------------------
# Main targets
#---------------------------------------------------------------------------------
default: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(LD) $(LD_FLAGS) $(LIB_PATH) -o $@ $(OBJECTS) $(LIBS)

$(TARGET): $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) $< -O binary $@

#---------------------------------------------------------------------------------
# Object generation
#---------------------------------------------------------------------------------
$(BUILD_DIR)/%.o : src/%.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o : src/kolibrios/%.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: default clean