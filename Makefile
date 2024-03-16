a3p1: a3p1.c
	gcc -w a3p1.c -o a3p1
a3p2: a3p2.c
	gcc -w a3p2.c -o a3p2

a3p3-comm: a3p3-comm.c
	gcc -w a3p3-comm.c -o a3p3-comm

a3p3-drop: a3p3-drop.c
	gcc -w a3p3-drop.c -o a3p3-drop

a3-compiles: a3-compiles.c
	gcc -w a3-compiles.c -o a3-compiles

tar:
	tar -czf Sebti-a3.tar *

clean:
	rm -f a3p1 a3p2 a3p3-drop a3p3-comm

	