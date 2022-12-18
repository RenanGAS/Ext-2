CC=g++ -Wall

PROGS=nEXT2shell

all: $(PROGS)

clean:
	rm -f $(PROGS)

nEXT2shell: nEXT2shell.cpp nEXT2shell.h
	$(CC) nEXT2shell.cpp -o nEXT2shell -lreadline

debug:
	$(CC) nEXT2shell.cpp -o nEXT2shell -lreadline
	./nEXT2shell
	rm -f $(PROGS)

backup:
	rm -f ./myext2image.img
	cp ./imagem/myext2image.img ./
