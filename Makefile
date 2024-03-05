all:
	gcc -std=c11 -O3 -Wall -Werror -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wvla -I/usr/local/cuda/include -o nvml_direct_access nvml_direct_access.c -lpci -lnvidia-ml
clean:
	rm -f nvml_direct_access
install:
	cp nvml_direct_access /usr/local/bin/
