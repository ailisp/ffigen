OS = $(shell uname -s)
NOW = $(shell date -u "+%F-%H-%M-%S")
TAR = tar
EXE =

GCC_VERSION = 7.2.0

ifeq ($(OS),Linux)
ARCH = $(shell uname -m)
ifeq ($(ARCH), ppc64)
PLATFORM = linuxppc
CONFIGTARGET = ppc64-unknown-linux
CONFIGARGS = --target=$(CONFIGTARGET) --host=$(CONFIGTARGET) --with-cpu=default32 --enable-biarch --enable-threads=posix
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif
ifeq ($(ARCH), ppc)
PLATFORM = linuxppc
CONFIGTARGET = ppc64-unknown-linux
CONFIGARGS = --target=$(CONFIGTARGET) --host=$(CONFIGTARGET) --with-cpu=default32 --enable-biarch --enable-threads=posix
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif
ifeq ($(ARCH), x86_64)
PLATFORM = linuxx8664
CONFIGTARGET = x86_64-pc-linux
CONFIGARGS = --target=$(CONFIGTARGET) --host=$(CONFIGTARGET) --enable-threads=posix --disable-multilib
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif
ifeq ($(ARCH), i686)
PLATFORM = linuxx8664
CONFIGTARGET = i686-unknown-linux
CONFIGARGS = --target=$(CONFIGTARGET) --host=$(CONFIGTARGET) --enable-threads=posix --enable-biarch
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif
ifeq ($(ARCH), armv7l)
PLATFORM = linuxarm
CONFIGTARGET = armv7l-unknown-linux-gnu
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif
endif

ifeq ($(OS),FreeBSD)
ARCH = $(shell uname -p)
ifeq ($(ARCH),amd64)
PLATFORM = freebsdx8664
CONFIGTARGET = amd64-unknown-freebsd6.0
else
PLATFORM = freebsdx86
CONFIGTARGET = i386-unknown-freebsd6.0
endif
CONFIGARGS = --target=$(CONFIGTARGET) --host=$(CONFIGTARGET) --enable-threads=posix --enable-biarch
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
endif


ifeq ($(OS),Darwin)
ARCH = $(shell arch)
PLATFORM=darwinppc
CONFIGTARGET = powerpc-apple-darwin8
CONFIGARGS = --target=$(CONFIGTARGET) --with-cpu=default32 --enable-biarch
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = NO
endif

ifeq ($(OS),SunOS)
TAR = gtar
PLATFORM = solarisx86
MAKE_FUNKY_LINK = YES
endif

# 32-bit Cygwin, at this point
ifeq (CYGWIN,$(findstring CYGWIN,$(OS)))
ARCH = $(shell arch)
PLATFORM=win32
CONFIGTARGET=i386-pc-cygwin
MAKE_FUNKY_LINK = YES
COPY_OBJC_HEADERS = YES
EXE = .exe
endif

all: package
	m4 -DPLATFORM=$(PLATFORM) -DGCC_VERSION=$(GCC_VERSION)-$(NOW) source/INSTALL-$(PLATFORM).m4 > INSTALL-FFIGEN-$(PLATFORM)-gcc-$(GCC_VERSION)-$(NOW).txt


compile: 
	mkdir build
	(cd build ; ../gcc-$(GCC_VERSION)/configure --enable-languages=objc $(CONFIGARGS) )
ifeq ($(MAKE_FUNKY_LINK),YES)
ifeq ($(OS),Darwin)
	(cd build ; ln -s . build-`../gcc-$(GCC_VERSION)/config.guess`)
endif
ifeq ($(OS),Linux)
	(cd build ; ln -s . build-$(CONFIGTARGET))
endif
ifeq ($(OS),FreeBSD)
	(cd build ; ln -s . build-$(CONFIGTARGET))
endif
ifeq ($(OS),SunOS)
	(cd build ; ln -s . build-`../gcc-$(GCC_VERSION)/config.guess`)
endif
ifeq ($(PLATFORM),win32)
	(cd build ; ln -s . build-`../gcc-$(GCC_VERSION)/config.guess`)
endif
endif
	(cd build ; $(MAKE) maybe-configure-libiberty maybe-configure-gcc maybe-configure-libcpp)
	(cd build/libiberty ; $(MAKE))
	(cd build/intl ; $(MAKE))
	(cd build/libcpp ; $(MAKE))
	(cd build/gcc ; $(MAKE) cc1obj$(EXE) xlimits.h)

package: compile
	mkdir bin
	cat source/$(PLATFORM)-gcc-$(GCC_VERSION)-h-to-ffi.sh source/h-to-ffi-common >  bin/h-to-ffi.sh
	chmod +x bin/h-to-ffi.sh
	mkdir ffigen
	cp -r -p gcc-$(GCC_VERSION)/gcc/ginclude ffigen
	mv ffigen/ginclude ffigen/include
	cp -p build/gcc/xlimits.h ffigen/include/limits.h
	cp -p gcc-$(GCC_VERSION)/gcc/gsyslimits.h ffigen/include/syslimits.h
ifeq ($(COPY_OBJC_HEADERS), YES)
	cp -r -p gcc-$(GCC_VERSION)/libobjc/objc ffigen/include
endif
	mkdir ffigen/bin
	cp -p build/gcc/cc1obj$(EXE) ffigen/bin/ffigen$(EXE)
	strip ffigen/bin/ffigen$(EXE)
	$(TAR) cfz ffigen-bin-$(PLATFORM)-gcc-$(GCC_VERSION)-$(NOW).tar.gz bin ffigen

clean:
	rm -rf ffigen build bin ffigen*tar.gz INSTALL-FFIGEN-$(PLATFORM)-gcc-$(GCC_VERSION)*.txt
