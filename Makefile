CC=g++ -Wall

PROGS=super

SUPER=super

CLEAN=clean

all: $(PROGS)

clean:
	rm -f $(PROGS)

super: ext2super.cpp ext2.h
	$(CC) ext2super.cpp -o super -lreadline

debug:
	$(CC) ext2super.cpp -o super -lreadline
	./super
	rm -f $(PROGS)
