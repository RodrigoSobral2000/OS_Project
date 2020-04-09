projeto: gestor.o
	gcc -Wall -Wextra -pthread -o projeto gestor.o

gestor.o:gestor.c header.h
	gcc -Wall -Wextra -pthread -c gestor.c
