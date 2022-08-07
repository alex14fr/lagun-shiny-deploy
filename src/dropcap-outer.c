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
 
	if(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET)<0) { perror("unshare"); }
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
	if(mount("/tmp/priv/terminal","/tmp",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	if(mount("/apps/terminal","/apps",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind"); exit(1); }
	for(int i=0;i<1024;i++) {
		if(i!=CAP_SETFCAP) {
			prctl(PR_CAPBSET_DROP, i);
		}
	}
	execvp(argv[1],argv+1);
}
