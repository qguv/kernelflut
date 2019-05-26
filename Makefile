#
# Copyright (c) 2015 - 2016 DisplayLink (UK) Ltd.
#

OBJ = evdi/library/libevdi.so thinkpad.o pixelflut.o evdi.o kernelflut.o
DEPS = evdi.h pixelflut.h kernelflut.h
CFLAGS := -I. -Ievdi/library -Levdi/library -levdi -Wall -Wpedantic -Wextra -Werror -std=gnu99 -g $(CFLAGS)

.PHONY: build
build: kernelflut

kernelflut: $(OBJ)
	$(CC) -o "$@" $^ $(CFLAGS) $(LIBS)

%.o: %.edid
	ld -r -b binary -o "$@" "$<"
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents "$@" "$@"

%.o: %.c $(DEPS)
	$(CC) -c -o "$@" "$<" $(CFLAGS)

evdi/library/libevdi.so:
	make -C evdi/library

evdi/module/evdi.ko:
	make -C evdi/module

.PHONY: clean
clean:
	rm -f kernelflut *.o
	make -C evdi/library clean
	make -C evdi/module clean

.PHONY: update_evdi
update_evdi:
	git subtree pull --prefix evdi https://github.com/DisplayLink/evdi master --squash
