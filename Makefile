# 编译

LIBNAME = waybar_cffi_lyrics

TARGET = libwaybar_cffi_lyrics.so
BUILD_DIR = build

# 通过参数设置 DESTDIR
# 例如：make install DESTDIR=/apps/libs/
DESTDIR = ~/.config/cffi/

ALL = $(TARGET)

$(TARGET):
	@meson setup $(BUILD_DIR)
	@meson compile -C $(BUILD_DIR) $(LIBNAME)
	@echo "Build complete!"

debug:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) $(LIBNAME)
	@echo "Build complete!"

demos:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) demo

playerDemo:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) playerDemo
sigDemo:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) sigDemo

install:
	@if [ ! -d $(DESTDIR) ]; then \
		mkdir -p $(DESTDIR); \
	fi
	@if [ -f $(DESTDIR)/${TARGET} ]; then \
	    mv $(DESTDIR)/${TARGET} $(DESTDIR)/${TARGET}.bak; \
	fi
	cp $(BUILD_DIR)/${TARGET} $(DESTDIR)
	@echo "Install complete!"

clean:
	rm -f $(BUILD_DIR)/${TARGET}

purge:
	rm -rf $(BUILD_DIR)