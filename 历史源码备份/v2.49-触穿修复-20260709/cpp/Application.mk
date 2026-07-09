APP_ABI := arm64-v8a
APP_PLATFORM := android-22
APP_STL := c++_static
APP_OPTIM := release
APP_CFLAGS := -O3 -ffast-math -fomit-frame-pointer
APP_CFLAGS += -fno-stack-protector -fvisibility=hidden
APP_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
APP_CFLAGS += -fno-rtti -fno-exceptions
APP_CFLAGS += -frandom-seed=build_id -fno-ident -fno-builtin
APP_CFLAGS += -march=armv8-a+crc+crypto -mtune=cortex-a75
APP_CFLAGS += -D_FORTIFY_SOURCE=2
APP_CPPFLAGS := -std=c++20 $(APP_CFLAGS)
APP_LDFLAGS := -O3 -Wl,--gc-sections -Wl,--exclude-libs,ALL
APP_LDFLAGS += -Wl,-Bsymbolic -Wl,--build-id=none