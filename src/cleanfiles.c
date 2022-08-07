#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define CMD "find /tmp/priv/terminal/priv* -ctime +1 -delete"

int main(int argc, char **argv) {
	while(1) {
		sleep(3600);
		printf("Cleaning files in /tmp...\n");
		system(CMD);
	}
}

