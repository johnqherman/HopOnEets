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

pack:
	EETSMOD_WINLIBS="-lws2_32 -lwinhttp" eetsmod pack $(MOD).cpp -o $(MOD).eetsmod

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT) $(MOD).eetsmod

.PHONY: all win pack clean
