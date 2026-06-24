# null FNA3D backend for the headless re-sim verifier (docs/headless-resim.md).
all: libnullbackend.so

libnullbackend.so: fna3d_null.c
	$(CC) -O2 -fPIC -shared -fvisibility=hidden -o $@ $<

clean:
	rm -f libnullbackend.so

.PHONY: all clean
