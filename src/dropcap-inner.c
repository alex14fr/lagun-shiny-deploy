/**
 * BSD 2-Clause License
 * Copyright (c) 2022, Alexandre Janon <alex14fr@gmail.com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>

// create new NSes, bind mount /tmp and /dev/pts to empty directories (unless env TERM_SHARE_FILES is set), and exec without any capability

int main(int argc, char **argv) {
	char tmp[256];
	int fd;
	if(argc<2) { printf("Usage: [TERM_SHARE_FILES=1] %s prog\n", argv[0]); exit(1); }
 
	if(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | /*CLONE_NEWIPC |*/ CLONE_NEWNET)<0) { perror("unshare"); }
	int sock=socket(AF_INET,SOCK_STREAM,0);
	struct ifreq ifr;
	memcpy(ifr.ifr_name, "lo\0", 3);
	ifr.ifr_flags=IFF_UP|IFF_LOOPBACK|IFF_RUNNING;
	if(ioctl(sock, SIOCSIFFLAGS, &ifr)<0) { perror("ioctl"); }
	close(sock);
	if(fork()>0) { wait(NULL); exit(0); }
	if(mount("proc","/proc","proc",0,NULL)<0) { perror("mount proc"); exit(1); }
	fd=open("/proc/self/setgroups",O_WRONLY);
	if(fd<0) { perror("open setgroups"); exit(1); }
	if(write(3,"deny",4)<0) { perror("write setgroups"); exit(1); }
	close(fd); 

	if(mount("none","/",NULL,MS_PRIVATE|MS_REC,NULL)<0) { perror("mount private /"); exit(1); }
	chdir("/");
	unsigned char rnd[6];
	if(getenv("TERM_SHARE_FILES")==NULL) 
		getrandom(rnd, 6, 0);
	else
		bzero(rnd, 6);
	snprintf(tmp, 256, "/tmp/priv%x%x%x%x%x%x", rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5]);
	mkdir("/tmp", 0777);
	if(mkdir(tmp, 0777)<0) { perror("mkdir"); }
	if(mount(tmp,"/tmp",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind /tmp"); exit(1); }
	mkdir("/tmp/pts",0777);
	mkdir("/dev",0777);
	mkdir("/dev/pts",0777);
	if(mount("/tmp/pts","/dev/pts",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind /dev/pts"); exit(1); }
	//mount("/tmp", "/tmp", NULL, MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_REMOUNT|MS_BIND, NULL);
	//mount("/dev/pts", "/dev/pts", NULL, MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_REMOUNT|MS_BIND, NULL);
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	prctl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS_LOCKED|SECBIT_NO_SETUID_FIXUP|SECBIT_NO_SETUID_FIXUP_LOCKED|SECBIT_NOROOT|SECBIT_NOROOT_LOCKED|SECBIT_NO_CAP_AMBIENT_RAISE|SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED, 0, 0, 0);
	for(int i=0;i<1024;i++) {
		prctl(PR_CAPBSET_DROP, i);
	}
	chdir("/tmp");
	if(execvp(argv[1],argv+1)<0) { perror("execvp"); exit(1); }
}
