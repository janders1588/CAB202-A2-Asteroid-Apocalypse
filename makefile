# CAB202 Teensy Makefile
# Lawrence Buckingham, September 2017.
# Queensland University of Technology.

# Replace these targets with your target (hex file) name, including the .hex part.

TARGETS = \
	main.c \
	cab202_adc.c\
	usb_serial.c

OUT = \
	main
# Set the name of the folder containing libcab202_teensy.a

CAB202_TEENSY_FOLDER = ../cab202_teensy

# ---------------------------------------------------------------------------
#	Leave the rest of the file alone.
# ---------------------------------------------------------------------------

all: $(OUT)

TEENSY_LIBS = -lcab202_teensy -lprintf_flt -lm 
TEENSY_DIRS =-I$(CAB202_TEENSY_FOLDER) -L$(CAB202_TEENSY_FOLDER)
TEENSY_FLAGS = \
	-std=gnu99 \
	-mmcu=atmega32u4 \
	-DF_CPU=8000000UL \
	-Wl,-u,vfprintf \
	-Os 

clean:
	for f in $(OUT); do \
		if [ -f $$f.hex]; then rm $$f.hex; fi; \
		if [ -f $$f.elf ]; then rm $$f.elf; fi; \
		if [ -f $$f.obj ]; then rm $$f.obj; fi; \
	done

rebuild: clean all

%.c:
	avr-gcc $(TARGETS) $(TEENSY_FLAGS) $(TEENSY_DIRS) $(TEENSY_LIBS) -o $(OUT).obj
	avr-objcopy -O ihex $(OUT).obj $(OUT).hex