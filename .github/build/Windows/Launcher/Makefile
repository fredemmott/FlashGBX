CC      = gcc.exe
WINDRES = windres.exe

CFLAGS  = -O3 -std=c99 -municode
LDFLAGS = -municode -mwindows -static-libgcc -s -frandom-seed=0 \
		-Wl,--build-id=none -Wl,--no-insert-timestamp

all: launcher.exe

launcher.exe: FlashGBX_Launcher.o FlashGBX_Launcher.res
	$(CC) $^ -o $@ $(LDFLAGS)

FlashGBX_Launcher.o: FlashGBX_Launcher.c
	$(CC) $(CFLAGS) -c $< -o $@

FlashGBX_Launcher.res: FlashGBX_Launcher.rc
	$(WINDRES) -i $< --input-format=rc -o $@ -O coff

clean:
	-del *.o *.res launcher.exe

.PHONY: all clean
