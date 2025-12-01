CC := g++
CFLAGS := -O3 -std=c++17 -Wall -Wextra -pedantic
LDFLAGS := -lz

all: encode decode

encode: src/encode.cpp src/miniz.c
	$(CC) $(CFLAGS) -Isrc src/encode.cpp src/miniz.c -o encode $(LDFLAGS)

decode: src/decode.cpp src/miniz.c
	$(CC) $(CFLAGS) -Isrc src/decode.cpp src/miniz.c -o decode $(LDFLAGS)

clean:
	rm -f encode decode

.PHONY: all clean

