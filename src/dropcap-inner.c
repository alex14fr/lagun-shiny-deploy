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

// create new NSes, bind mount /tmp and /dev/pts to empty directories (unless env TERM_SHARE_FILES is set), and exec without any capability

int main(int argc, char **argv) {
	char tmp[256];
	int fd;
	if(argc<2) { printf("Usage: [TERM_SHARE_FILES=1] %s prog\n", argv[0]); exit(1); }
 
	if(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWNET)<0) { perror("unshare"); }
	if(fork()>0) { wait(NULL); exit(0); }
	fd=open("/proc/self/setgroups",O_WRONLY);
	if(fd<0) { perror("open setgroups"); exit(1); }
	if(write(3,"deny",4)<0) { perror("write setgroups"); exit(1); }
	close(fd);

	if(mount("none","/",NULL,MS_PRIVATE|MS_REC,NULL)<0) { perror("mount private /"); exit(1); }
	chdir("/");
	if(getenv("TERM_SHARE_FILES")!=NULL) {
		unsigned char rnd[6];
		getrandom(rnd, 6, 0);
		snprintf(tmp, 256, "/tmp/priv%x%x%x%x%x%x", rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5]);
		mkdir("/tmp", 0777);
		if(mkdir(tmp, 0777)<0) { perror("mkdir"); }
		if(mount(tmp,"/tmp",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind /tmp"); exit(1); }
		mkdir("/tmp/pts",0777);
		mkdir("/dev",0777);
		mkdir("/dev/pts",0777);
		if(mount("/tmp/pts","/dev/pts",NULL,MS_PRIVATE|MS_BIND,NULL)<0) { perror("mount bind /dev/pts"); exit(1); }
	}
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	prctl(PR_SET_SECUREBITS, SECBIT_KEEP_CAPS_LOCKED|SECBIT_NO_SETUID_FIXUP|SECBIT_NO_SETUID_FIXUP_LOCKED|SECBIT_NOROOT|SECBIT_NOROOT_LOCKED|SECBIT_NO_CAP_AMBIENT_RAISE|SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED, 0, 0, 0);
	for(int i=0;i<1024;i++) {
		prctl(PR_CAPBSET_DROP, i);
	}
	chdir("/tmp");
	execvp(argv[1],argv+1);
}
