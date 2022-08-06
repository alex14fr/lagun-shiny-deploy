#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

static char req[]="GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
static char reqgz[]="GET / HTTP/1.1\r\nAccept-encoding: gzip\r\nHost: localhost\r\n\r\n";

void timeout(int sig) {
	exit(0);
}

int main(int argc, char **argv) {
	if(argc<3) { printf("Usage: %s socket time [gzip]\n", argv[0], argv[1]); exit(1); } 
	int s=socket(AF_UNIX, SOCK_STREAM, 0);
	if(s<0) { perror("socket"); exit(1); }
	struct sockaddr_un saddr;
	saddr.sun_family=AF_UNIX;
	strlcpy(saddr.sun_path,argv[1],108);
	int ntry;
	for(ntry=0; ntry<100; ntry++) {
		if(connect(s, (struct sockaddr*)&saddr, sizeof(struct sockaddr_un))>=0) 
			break;
		sleep(2);
	}
	if(ntry==100) { perror("connect"); exit(1); }
	char *reqq=(argc<4 ? req : reqgz);
	if(write(s, reqq, strlen(reqq))<0) { perror("write"); exit(1); }
	signal(SIGALRM, timeout);
	alarm(atoi(argv[2]));
	int nr;
	char buf[4096];
	while((nr=read(s, buf, 4096))>=0) {
		write(STDOUT_FILENO, buf, nr);
	}
}
