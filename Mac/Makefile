# Mineways macOS ARM64 build  (wxWidgets GUI)
# Usage: make -C Mac          (from repo root)

CXX      := clang++

MINEWAYS_VERSION ?= 13.0
MINEWAYS_VERSION_MAJOR := $(word 1,$(subst ., ,$(MINEWAYS_VERSION)))
MINEWAYS_VERSION_MINOR_RAW := $(word 2,$(subst ., ,$(MINEWAYS_VERSION)))
MINEWAYS_VERSION_MINOR := $(shell echo $(MINEWAYS_VERSION_MINOR_RAW) | sed 's/^0*//;s/^$$/0/')
VERSION_HEADER := .MinewaysVersion.h

WX_CFLAGS := $(shell wx-config --cxxflags)
WX_LIBS   := $(shell wx-config --libs)

CXXFLAGS := -std=c++17 -arch arm64 -O2 -Wall \
            -MMD -MP \
            -Wno-unused-function \
            -Wno-macro-redefined \
            -Wno-writable-strings \
            -DMINEWAYS_X64 \
            -I. -I../Win \
            -include $(VERSION_HEADER) \
            -include compat.h \
            $(shell pkg-config --cflags zlib 2>/dev/null)

# GUI files need wxWidgets flags on top of the base flags
GUI_CXXFLAGS := $(CXXFLAGS) $(WX_CFLAGS)

LDFLAGS  := -arch arm64
LIBS     := -lz $(WX_LIBS)

SRC_DIR  := ../Win

# Core library (no GUI, no Win32 UI calls)
CORE_SRCS := $(SRC_DIR)/blockInfo.cpp \
             $(SRC_DIR)/biomes.cpp \
             $(SRC_DIR)/lodepng.cpp \
             $(SRC_DIR)/rwpng.cpp \
             $(SRC_DIR)/nbt.cpp \
             $(SRC_DIR)/region.cpp \
             $(SRC_DIR)/cache.cpp \
             $(SRC_DIR)/ObjFileManip.cpp \
             $(SRC_DIR)/terrainExtData.cpp \
             $(SRC_DIR)/stdafx.cpp \
             $(SRC_DIR)/MinewaysMap.cpp

# wxWidgets GUI sources (live in Mac/)
GUI_SRCS  := MinewaysApp.cpp \
             MinewaysFrame.cpp \
             MapPanel.cpp \
             LocationDialog.cpp \
             ExportDialog.cpp \
             MacCullingSchemes.cpp \
             ImportSettings.cpp

CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp, %.o, $(CORE_SRCS))
GUI_OBJS  := $(patsubst %.cpp, %.o, $(GUI_SRCS))
ALL_OBJS  := $(CORE_OBJS) $(GUI_OBJS)
ALL_DEPS  := $(ALL_OBJS:.o=.d)

TARGET    := mineways
APP_NAME  := Mineways.app
APP_CONTENTS := $(APP_NAME)/Contents

.PHONY: all app clean FORCE

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built $(TARGET)"

app: $(TARGET)
	rm -rf $(APP_NAME)
	mkdir -p $(APP_CONTENTS)/MacOS $(APP_CONTENTS)/Resources
	cp $(TARGET) $(APP_CONTENTS)/MacOS/
	cp Info.plist $(APP_CONTENTS)/
	/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(MINEWAYS_VERSION)" $(APP_CONTENTS)/Info.plist
	/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(MINEWAYS_VERSION)" $(APP_CONTENTS)/Info.plist
	cp mineways.icns $(APP_CONTENTS)/Resources/
	@if [ -f terrainExt.png ]; then cp terrainExt.png $(APP_CONTENTS)/Resources/; echo "Bundled terrainExt.png"; fi
	@if [ -f ../terrainExt.png ]; then cp ../terrainExt.png $(APP_CONTENTS)/Resources/; echo "Bundled terrainExt.png from repo root"; fi
	@echo "Built $(APP_NAME)"

COMPAT_H := compat.h

FORCE:

$(VERSION_HEADER): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' \
		'#define MAC_MINEWAYS_VERSION_STRING "$(MINEWAYS_VERSION)"' \
		'#define MAC_MINEWAYS_VERSION_MAJOR $(MINEWAYS_VERSION_MAJOR)' \
		'#define MAC_MINEWAYS_VERSION_MINOR $(MINEWAYS_VERSION_MINOR)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(ALL_OBJS): $(VERSION_HEADER)

# Core files: base flags only; compat.h is force-included so all core objects depend on it
%.o: $(SRC_DIR)/%.cpp $(COMPAT_H)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# GUI files: base + wx flags (explicit pattern rules override the core rule above)
MinewaysApp.o: MinewaysApp.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

MinewaysFrame.o: MinewaysFrame.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

MapPanel.o: MapPanel.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

LocationDialog.o: LocationDialog.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

ExportDialog.o: ExportDialog.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

MacCullingSchemes.o: MacCullingSchemes.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

ImportSettings.o: ImportSettings.cpp $(COMPAT_H)
	$(CXX) $(GUI_CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o *.d $(TARGET) $(VERSION_HEADER)

-include $(ALL_DEPS)
