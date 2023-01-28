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

// create new NSes, bind mount /tmp and /dev/pts to empty directories, and exec without any capability

int main(int argc, char **argv) {
	char tmp[256];
	if(argc<3) { printf("Usage: %s appname prog\n", argv[0]); exit(1); }
 
	if(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | /*CLONE_NEWIPC |*/ CLONE_NEWNET)<0) { perror("unshare"); }
	if(fork()>0) { wait(NULL); exit(0); }
	/*
	fd=open("/proc/self/setgroups",O_WRONLY);
	if(fd<0) { perror("open setgroups"); exit(1); }
	if(write(3,"deny",4)<0) { perror("write setgroups"); exit(1); }
	close(fd);
   */
	if(mount("none","/",NULL,MS_PRIVATE|MS_REC,NULL)<0) { perror("mount private /"); exit(1); }
	chdir("/");
	mkdir("/tmp",0777);
	mkdir("/tmp/priv",0777);
	snprintf(tmp,256,"/tmp/priv/%s",argv[1]);
	mkdir(tmp,0777);
	if(mount(tmp,"/var/run",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { }
	if(mount(tmp,"/tmp",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	//mount("/tmp", "/tmp", NULL, MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_REMOUNT|MS_BIND, NULL);
	snprintf(tmp,256,"/apps/%s",argv[1]);
	if(mount(tmp,"/apps",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	prctl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS_LOCKED|SECBIT_NO_SETUID_FIXUP|SECBIT_NO_SETUID_FIXUP_LOCKED|SECBIT_NOROOT|SECBIT_NOROOT_LOCKED|SECBIT_NO_CAP_AMBIENT_RAISE|SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED, 0, 0, 0);
	for(int i=0;i<256;i++) {
		prctl(PR_CAPBSET_DROP, i);
	}
	chdir("/apps");
	if(execvp(argv[2],argv+2)<0) { perror("execvp"); exit(1); }
}
