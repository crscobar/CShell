TARGETS=spawnshell forkshell

spawnshell: spawnshell.c
	gcc -g -Wall -o spawnshell spawnshell.c

forkshell: forkshell.c
	gcc -g -Wall -o forkshell forkshell.c

clean:
	rm -f $(TARGETS)