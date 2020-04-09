RM=rm -rf
CC=gcc

C_SRCS=$(shell find . -iname "*.c" | tr '\n' ' ')
H_SRCS=$(shell find . -iname "*.h" | tr '\n' ' ')
OBJS=$(C_SRCS:./%.c=build/%.o)

# Clean and compile .so
all: build/liblibrary.so

create-dirs:
	mkdir -p build/library/

build/liblibrary.so: create-dirs $(OBJS)
	$(CC) -shared -o "build/liblibrary.so" $(OBJS)

build/library/%.o: library/%.c
	$(CC) -c -fmessage-length=0 -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"

# Add debug parameters and compile
debug: CC += -DDEBUG -g
debug: all

# Clean build files
clean:
	$(RM) build

install: all
	cp -u build/liblibrary.so /usr/lib
	cp --parents -u $(H_SRCS) /usr/include

uninstall:
	rm -f /usr/lib/liblibrary.so
	rm -rf /usr/include/library

.PHONY: all create-dirs clean install uninstall
