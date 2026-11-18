CC = cc
TARGET = mydcimfiles_gtk
SRC = main.c

# Folosim backticks pentru BSD make
CFLAGS = -Wall -O2 `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0`

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
	
