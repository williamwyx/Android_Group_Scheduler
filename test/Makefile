CC := /home/w4118/utils/arm-2013.11/bin/arm-none-linux-gnueabi-gcc
LD := /home/w4118/utils/arm-2013.11/bin/arm-none-linux-gnueabi-gcc
CFLAGS := -g -Wall -static
LDFLAGS := -static
ADB := /home/w4118/utils/android-sdk-linux/platform-tools/adb

OBJECTS := test.o printer.o

all: test
	@$(ADB) push test /data/misc
	@$(ADB) shell /data/misc/test

ptest: printer
	@$(ADB) push printer /data/misc
	@$(ADB) shell /data/misc/printer

: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c 
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f test 
	rm -f test.o

.PHONY: clean
