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
#include <sys/wait.h>
#include <sys/stat.h>

// create new NSes, hide /tmp and some /apps, exec after dropping all caps but CAP_SETFCAP (for uid-mapped nested user ns)

int main(int argc, char **argv) {
	int origUid=geteuid(); 
	int origGid=getegid(); 
	char tmp[256];
	int fd;
	if(argc<2) { printf("Usage: %s prog\n", argv[0]); exit(1); }
 
	if(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | /*CLONE_NEWIPC |*/ CLONE_NEWNET)<0) { perror("unshare"); }
	if(fork()>0) { wait(NULL); exit(0); }
	fd=open("/proc/self/setgroups",O_WRONLY);
	if(fd<0) { perror("open setgroups"); exit(1); }
	if(write(3,"deny",4)<0) { perror("write setgroups"); exit(1); }
	close(fd);
	snprintf(tmp, 256, "0 %d 1\n", origUid);
	fd=open("/proc/self/uid_map",O_WRONLY);
	if(fd<0) { perror("open uid_map"); exit(1); }
	if(write(fd,tmp,strlen(tmp))<0) { perror("write uid_map"); exit(1); }
	close(fd);
	snprintf(tmp, 256, "0 %d 1\n", origGid);
	fd=open("/proc/self/gid_map",O_WRONLY);
	if(fd<0) { perror("open gid_map"); exit(1); }
	if(write(fd,tmp,strlen(tmp))<0) { perror("gid_map"); exit(1); }
	close(fd);
	if(mount("none","/",NULL,MS_PRIVATE|MS_REC,NULL)<0) { perror("mount private /"); exit(1); }
	if(mount("none","/dev/pts","devpts",MS_PRIVATE,NULL)<0) { perror("mount devpts"); exit(1); }
	chdir("/");
	mkdir("/tmp",0777);
	mkdir("/tmp/priv",0777);
	mkdir("/tmp/priv/terminal",0777);
	if(mount("/tmp/priv/terminal","/var/run",NULL,MS_PRIVATE|MS_BIND,NULL)<0) {  }
	if(mount("/tmp/priv/terminal","/tmp",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	if(mount("/apps/terminal","/apps",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	for(int i=0;i<1024;i++) {
		if(i!=CAP_SETFCAP) {
			prctl(PR_CAPBSET_DROP, i);
		}
	}
	if(execvp(argv[1],argv+1)<0) { perror("execvp"); exit(1); }
}
