ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := kdump.elf

CFLAGS := -Wall -Werror

all: $(ELF)

SRCS := main.c
OBJS := $(SRCS:.c=.o)

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	strip --strip-all $(ELF)
	rm -f *.o
	chmod 600 $(ELF)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(ELF) *.o
