CC := g++
CFLAGS := -O3 -std=c++17 -Wall -Wextra -pedantic

all: encode decode

encode: src/encode.cpp src/rangecoder.h src/codec.h
	$(CC) $(CFLAGS) -Isrc src/encode.cpp -o encode

decode: src/decode.cpp src/rangecoder.h src/codec.h
	$(CC) $(CFLAGS) -Isrc src/decode.cpp -o decode

clean:
	rm -f encode decode

.PHONY: all clean
