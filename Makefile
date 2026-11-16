CXX ?= g++
CPPFLAGS += -Isrc
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
LDLIBS += -pthread -lrt -lm
GIO_CFLAGS = $(shell pkg-config --cflags gio-2.0)
GIO_LIBS = $(shell pkg-config --libs gio-2.0)
DRM_LIBS = $(shell pkg-config --libs libdrm 2>/dev/null)
ifneq ($(strip $(DRM_LIBS)),)
build/src/drm_renderer.o: CPPFLAGS += -DSR_HAVE_DRM $(shell pkg-config --cflags libdrm)
endif
CORE = src/framebuffer.cpp src/renderer.cpp
OBJECTS = $(CORE:%.cpp=build/%.o)
GATEWAY = src/mesh/gateway.cpp src/mesh/bluez.cpp src/screens/readings.cpp src/web.cpp
GATEWAY_OBJECTS = $(GATEWAY:%.cpp=build/%.o)

.DEFAULT_GOAL := all
.PHONY: all test
all: build/smartrabbit-test
build/smartrabbit-test: LDLIBS += $(GIO_LIBS) $(DRM_LIBS)
build/src/mesh/bluez.o: CPPFLAGS += $(GIO_CFLAGS)
build/smartrabbit-test: $(OBJECTS) $(GATEWAY_OBJECTS) build/src/main.o build/src/lcd_renderer.o build/src/drm_renderer.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/smartrabbit-tests: $(OBJECTS) build/src/screens/readings.o build/tests/test.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/smartrabbit-network-tests: build/src/framebuffer.o build/src/screens/readings.o build/tests/network_test.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/smartrabbit-menu-tests: build/src/mesh/gateway.o build/src/web.o build/tests/gateway_menu_test.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
test: build/smartrabbit-tests build/smartrabbit-network-tests build/smartrabbit-menu-tests
	./build/smartrabbit-tests
	./build/smartrabbit-network-tests
	./build/smartrabbit-menu-tests
-include build/tests/network_test.d
-include $(OBJECTS:.o=.d) $(GATEWAY_OBJECTS:.o=.d) build/src/main.d build/src/lcd_renderer.d build/src/drm_renderer.d build/tests/test.d

-include build/tests/gateway_menu_test.d
