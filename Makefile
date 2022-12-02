CC=gcc -Wall

PROGS=super

all: $(PROGS)

clean:
	rm -f $(PROGS)

super: ext2super.c ext2.h
	$(CC) ext2super.c -o super 
