ZIG = zig c++
TARGET = ProxyBK_DialogLoader003
CXXFLAGS = -shared -fPIC -I ./include

all: linux windows macos

SRCS := $(wildcard src/*.c) $(wildcard src/*.cpp)

linux:
	$(ZIG) -target x86_64-linux-gnu $(CXXFLAGS) -ldl -o $(TARGET).so $(SRCS)

windows:
	$(ZIG) -target x86_64-windows $(CXXFLAGS) -s -o $(TARGET).dll $(SRCS)

macos:
	$(ZIG) -target aarch64-macos $(CXXFLAGS) -o $(TARGET).dylib $(SRCS)

.PHONY: all linux windows macos
