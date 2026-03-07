PLUGIN_NAME := nt_puddle
DSP_LIB := puddle_dsp
TEST_NAME := test_puddle
API_PATH ?= distingNT_API
ARM_TOOLCHAIN ?= arm-none-eabi-
UNAME_S := $(shell uname -s)

BUILD_DIR := build
PLUGIN_DIR := plugins
TARGET ?= hardware

TEST_CXX ?= g++
TEST_CXXFLAGS := -std=c++14 -Wall -Wextra -pedantic -O2 -I.

PLUGIN_SOURCES := nt_puddle.cpp $(DSP_LIB).cpp
UNIT_TEST_SOURCES := test_puddle.cpp $(DSP_LIB).cpp

ifeq ($(TARGET),hardware)
	CXX := $(ARM_TOOLCHAIN)g++
	CXXFLAGS := -std=gnu++14 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
	CXXFLAGS += -Os -Wall -ffunction-sections -fdata-sections
	CXXFLAGS += -fPIC -fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
	CXXFLAGS += -DPUDDLE_NO_HEAP -I. -I$(API_PATH)/include
	LDFLAGS := -r
	OUTPUT := $(PLUGIN_DIR)/$(PLUGIN_NAME).o
	CHECK_CMD := $(ARM_TOOLCHAIN)nm $(OUTPUT) | grep ' U ' || echo "No undefined symbols"
	SIZE_CMD := $(ARM_TOOLCHAIN)size -A $(OUTPUT)
endif

ifeq ($(TARGET),plugin-test)
	ifeq ($(UNAME_S),Darwin)
		CXX := clang++
		CXXFLAGS := -std=gnu++14 -fPIC -Os -Wall -fno-rtti -fno-exceptions
		LDFLAGS := -dynamiclib -undefined dynamic_lookup
		EXT := dylib
	endif
	ifeq ($(UNAME_S),Linux)
		CXX := g++
		CXXFLAGS := -std=gnu++14 -fPIC -Os -Wall -fno-rtti -fno-exceptions
		LDFLAGS := -shared
		EXT := so
	endif
	ifeq ($(OS),Windows_NT)
		CXX := g++
		CXXFLAGS := -std=gnu++14 -fPIC -Os -Wall -fno-rtti -fno-exceptions
		LDFLAGS := -shared
		EXT := dll
	endif
	CXXFLAGS += -DPUDDLE_NO_HEAP -I. -I$(API_PATH)/include
	OUTPUT := $(PLUGIN_DIR)/$(PLUGIN_NAME).$(EXT)
	CHECK_CMD := nm $(OUTPUT) | grep ' U ' || echo "No undefined symbols"
	SIZE_CMD := ls -lh $(OUTPUT)
endif

PLUGIN_TEST_OUTPUT :=
ifeq ($(UNAME_S),Darwin)
	PLUGIN_TEST_OUTPUT := $(PLUGIN_DIR)/$(PLUGIN_NAME).dylib
endif
ifeq ($(UNAME_S),Linux)
	PLUGIN_TEST_OUTPUT := $(PLUGIN_DIR)/$(PLUGIN_NAME).so
endif
ifeq ($(OS),Windows_NT)
	PLUGIN_TEST_OUTPUT := $(PLUGIN_DIR)/$(PLUGIN_NAME).dll
endif

.PHONY: all test hardware plugin-test clean check size

all: test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PLUGIN_DIR):
	mkdir -p $(PLUGIN_DIR)

$(BUILD_DIR)/$(TEST_NAME): $(UNIT_TEST_SOURCES) $(DSP_LIB).h | $(BUILD_DIR)
	$(TEST_CXX) $(TEST_CXXFLAGS) $(UNIT_TEST_SOURCES) -o $@

test: $(BUILD_DIR)/$(TEST_NAME)
	./$(BUILD_DIR)/$(TEST_NAME)

HARDWARE_OBJECTS := $(BUILD_DIR)/nt_puddle.arm.o $(BUILD_DIR)/$(DSP_LIB).arm.o

$(BUILD_DIR)/nt_puddle.arm.o: nt_puddle.cpp $(DSP_LIB).h | $(BUILD_DIR)
	@test -d $(API_PATH)/include || (echo "Missing Disting NT API headers at $(API_PATH)/include"; exit 1)
	$(CXX) $(CXXFLAGS) -c nt_puddle.cpp -o $@

$(BUILD_DIR)/$(DSP_LIB).arm.o: $(DSP_LIB).cpp $(DSP_LIB).h | $(BUILD_DIR)
	@test -d $(API_PATH)/include || (echo "Missing Disting NT API headers at $(API_PATH)/include"; exit 1)
	$(CXX) $(CXXFLAGS) -c $(DSP_LIB).cpp -o $@

$(OUTPUT): $(HARDWARE_OBJECTS) | $(PLUGIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(HARDWARE_OBJECTS)

hardware:
	@$(MAKE) TARGET=hardware $(OUTPUT)

plugin-test:
	@$(MAKE) TARGET=plugin-test $(PLUGIN_TEST_OUTPUT)

check: $(OUTPUT)
	@$(CHECK_CMD)

size: $(OUTPUT)
	@$(SIZE_CMD)

clean:
	rm -rf $(BUILD_DIR) $(PLUGIN_DIR)
