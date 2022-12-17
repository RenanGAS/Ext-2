CC=g++ -Wall

PROGS=super

all: $(PROGS)

clean:
	rm -f $(PROGS)

super: ext2super.cpp ext2.h
	$(CC) ext2super.cpp -o super -lreadline

debug:
	$(CC) ext2super.cpp -o super -lreadline
	./super
	rm -f $(PROGS)

backup:
	rm -f ./myext2image.img
	cp ./imagem/myext2image.img ./
