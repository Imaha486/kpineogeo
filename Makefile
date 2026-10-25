# kpineogeo.kpi - cross build with MinGW-w64 (Linux / MSYS2)
#   make            -> bin/x86/kpineogeo.kpi, bin/x64/kpineogeo.kpi
#   make tools      -> bin/kpihost32.exe, bin/kpihost64.exe (test host)
#   make linux-tools-> bin/rip, bin/scan (native test tools, no SDK needed)
#   make package    -> dist/kpineogeo_vX.XX.zip
VERSION = 1.02

SDK  = third_party/kpisdk
SRCS = src/kpineogeo.cpp src/neoplayer.cpp src/songscan.cpp src/neosnd.cpp src/romfind.cpp \
       third_party/ymfm/ymfm_opn.cpp third_party/ymfm/ymfm_adpcm.cpp third_party/ymfm/ymfm_ssg.cpp
CORE = src/neoplayer.cpp src/songscan.cpp src/neosnd.cpp src/romfind.cpp \
       third_party/ymfm/ymfm_opn.cpp third_party/ymfm/ymfm_adpcm.cpp third_party/ymfm/ymfm_ssg.cpp
INC  = -Isrc -Ithird_party/ymfm -Ithird_party/z80 -I$(SDK)
CXXFLAGS = -O2 -std=c++17 -DUNICODE -D_UNICODE -DNOMINMAX $(INC) -Wno-attributes
LDFLAGS  = -shared -static -s -Wl,--kill-at -Wl,--enable-stdcall-fixup
HDRS = $(wildcard src/*.h)

CC32  = i686-w64-mingw32-gcc
CXX32 = i686-w64-mingw32-g++
CC64  = x86_64-w64-mingw32-gcc
CXX64 = x86_64-w64-mingw32-g++

all: bin/x86/kpineogeo.kpi bin/x64/kpineogeo.kpi

$(SDK)/kpi.h:
	@echo "KbMedia Player SDK headers not found. Run scripts/fetch-kpisdk.sh first." && false

bin/x86/kpineogeo.kpi: $(SDK)/kpi.h $(SRCS) third_party/z80/z80.c $(HDRS)
	mkdir -p bin/x86 obj/x86
	$(CC32) -O2 -c third_party/z80/z80.c -o obj/x86/z80.o
	$(CXX32) $(CXXFLAGS) $(SRCS) obj/x86/z80.o kpineogeo.def -o $@ $(LDFLAGS) -lshell32

bin/x64/kpineogeo.kpi: $(SDK)/kpi.h $(SRCS) third_party/z80/z80.c $(HDRS)
	mkdir -p bin/x64 obj/x64
	$(CC64) -O2 -c third_party/z80/z80.c -o obj/x64/z80.o
	$(CXX64) $(CXXFLAGS) $(SRCS) obj/x64/z80.o kpineogeo.def -o $@ $(LDFLAGS) -lshell32

tools: $(SDK)/kpi.h
	mkdir -p bin
	$(CXX64) $(CXXFLAGS) -municode -static tools/kpihost.cpp -o bin/kpihost64.exe
	$(CXX32) $(CXXFLAGS) -municode -static tools/kpihost.cpp -o bin/kpihost32.exe

linux-tools:
	mkdir -p bin obj/linux
	gcc -O2 -c third_party/z80/z80.c -o obj/linux/z80.o
	g++ -O2 -std=c++17 -Isrc -Ithird_party/ymfm -Ithird_party/z80 tools/rip.cpp  src/neosnd.cpp third_party/ymfm/ymfm_opn.cpp third_party/ymfm/ymfm_adpcm.cpp third_party/ymfm/ymfm_ssg.cpp obj/linux/z80.o -o bin/rip
	g++ -O2 -std=c++17 -Isrc -Ithird_party/ymfm -Ithird_party/z80 tools/scan.cpp $(CORE) obj/linux/z80.o -o bin/scan -lpthread

package: all
	rm -rf obj/pkg && mkdir -p obj/pkg/kpineogeo/x86 obj/pkg/kpineogeo/x64 obj/pkg/kpineogeo/licenses
	cp bin/x86/kpineogeo.kpi obj/pkg/kpineogeo/x86/
	cp bin/x64/kpineogeo.kpi obj/pkg/kpineogeo/x64/
	for f in readme.txt kpineogeo.ini; do iconv -f UTF-8 -t CP932 dist/$$f | sed 's/$$/\r/' > obj/pkg/kpineogeo/$$f; done
	cp LICENSE obj/pkg/kpineogeo/licenses/kpineogeo_LICENSE.txt
	cp third_party/ymfm/LICENSE obj/pkg/kpineogeo/licenses/ymfm_LICENSE.txt
	cp third_party/z80/LICENSE obj/pkg/kpineogeo/licenses/z80_LICENSE.txt
	cd obj/pkg && rm -f ../../dist/kpineogeo_v$(VERSION).zip && zip -qr ../../dist/kpineogeo_v$(VERSION).zip kpineogeo

clean:
	rm -rf bin obj dist/*.zip

.PHONY: all tools linux-tools package clean
