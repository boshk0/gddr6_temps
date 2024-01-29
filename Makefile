all:
	gcc -std=c11 -O3 -Wall -Werror -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wvla -I/usr/local/cuda/include -o gddr6 gddr6.c -lpci -lnvidia-ml
clean:
	rm -f gddr6
install:
	cp gddr6 /usr/local/bin/
