# Makefile for building a libusb-based C program

# Name of the output binary
TARGET = xvc_host

# Source files
SRCS = xvc_host.c

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags libusb-1.0)
LDFLAGS = $(shell pkg-config --libs libusb-1.0)

# Build target
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f $(TARGET)
