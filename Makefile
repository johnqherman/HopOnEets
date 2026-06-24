# Hop On Eets - build the mod against a sibling eets-mod-framework checkout.
MOD    = hop_on_eets
FW    ?= ../eets-mod-framework
INC    = $(FW)/include
WINLIB = $(FW)/build/libeetsmod.dll.a
OUT    = build

all: $(OUT)/$(MOD).so

$(OUT)/$(MOD).so: $(MOD).cpp $(wildcard src/*.h) | $(OUT)
	g++ -O2 -fPIC -std=c++17 -Wall -shared -I$(INC) -o $@ $< -ldl

win: $(OUT)/$(MOD).dll

$(OUT)/$(MOD).dll: $(MOD).cpp $(wildcard src/*.h) $(WINLIB) | $(OUT)
	i686-w64-mingw32-g++ -O2 -std=c++17 -Wall -shared -I$(INC) -o $@ $< $(WINLIB) -lws2_32 -lwinhttp

# one-shot bundle (.dll + .so + source + manifest + assets) via the framework CLI.
# EETSMOD_WINLIBS: the Windows .dll links winhttp (WebSocket client) + ws2_32.
pack:
	EETSMOD_WINLIBS="-lws2_32 -lwinhttp" eetsmod pack $(MOD).cpp -o $(MOD).eetsmod

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT) $(MOD).eetsmod

.PHONY: all win pack clean
