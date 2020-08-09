threadpool:threadpool.o
	gcc threadpool.c -o threadpool -pthread -g
threadpool.o:threadpool.c
	gcc -c threadpool.c

.PHONY:clean
clean:
	rm *.o threadpool
