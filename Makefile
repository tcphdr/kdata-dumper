ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := kdata_dumper.elf

CFLAGS := -Wall -Werror

all: $(ELF)

SRCS := main.c gpu.c
OBJS := $(SRCS:.c=.o)

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(ELF)
