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

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#ifdef TLS
#include <openssl/ssl.h>
#endif

#define MAXEVENTS 10
#define REQSZ 512
#define APPNAMESZ 24
#define HOME "/tmp/index.html"
//#define GCTEST
#ifdef GCTEST
#define GC_INTERVAL 5
#define GC_TIMEOUT 20
#define GC_VERBOSE
#else
#define GC_INTERVAL 3600
#define GC_TIMEOUT 86400
#endif

#define ERRMSG "HTTP/1.1 400\r\nConnection: close\r\n\r\n400 Bad request"
#define FORBIDMSG "HTTP/1.1 403\r\nConnection: close\r\n\r\n403 Forbidden"
#define NOTFOUNDMSG "HTTP/1.1 404\r\nContent-length: 13\r\n\r\n404 Not found"
#define BOOMSG "HTTP/1.1 500\r\nConnection: close\r\n\r\n500 Internal server error"

#define EPOLLMASK EPOLLIN|EPOLLRDHUP|EPOLLPRI|EPOLLHUP

struct conn {
	int fd;
	struct conn *other;
	char req[REQSZ]; // remote: head of request, local: appname
	int reqsz; // -1: fd is local socket, >0: fd is remote socket
	time_t created;
	//time_t last_seen;
	int sure_bind; // -1: local socket, remote sockets: 0: bind unsure/not done, 1: bind sure until next server answer, 2: bind always sure
#ifdef TLS
	SSL* ssl;
#endif
	struct conn *next;
};

static struct conn *g_hd; // pointer to head of global conn linked list 
static struct epoll_event events[MAXEVENTS]; // global event queue
static int nevents; // nr of events in ev queue 
static int curevent; // event in ev queue currently processed

#ifdef TLS
SSL_CTX *sslctx;
#endif

#ifdef INET6
#define FAM AF_INET6
#define STRCT struct sockaddr_in6
#define MMBRF sin6_family
#define MMBRA sin6_addr
#define MMBRP sin6_port
#else
#define FAM AF_INET
#define STRCT struct sockaddr_in
#define MMBRF sin_family
#define MMBRA sin_addr
#define MMBRP sin_port
#endif

int listen_sock(uint16_t port) {
	STRCT addr;
	int s=socket(FAM,SOCK_STREAM,0);
	if(s<0) { perror("socket"); exit(1); }
	setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
	fcntl(s,F_SETFL,O_NONBLOCK);
	addr.MMBRF=FAM;
	addr.MMBRP=htons(port);
	memset(&(addr.MMBRA), 0, sizeof(addr.MMBRA));
	if(bind(s,(struct sockaddr*)&addr,sizeof(STRCT))<0) { perror("bind"); exit(1); }
	if(listen(s,1)<0) { perror("listen"); exit(1); }
	return(s);
}

void insert_conn(struct conn *c) {
	c->next=g_hd;
	g_hd=c;
}

void list_conn(void) {
	if(g_hd==NULL) {
		fprintf(stderr, "no conn\n");
		return;
	}
	struct conn *e;
	time_t now=time(NULL);
	for(e=g_hd; e!=NULL; e=e->next) {
		fprintf(stderr, " connection %d, created %ld s ago\n", e->fd, now - e->created);
		if(e->other!=NULL) {
			fprintf(stderr, "   (bound to %d)\n", e->other->fd);
		}
	}
}

void remove_conn(struct conn *c) {
#ifdef GC_VERBOSE
	fprintf(stderr, "remove_conn() %d, before:\n", c->fd);
	list_conn();
#endif

	if(g_hd==NULL) return;
	if(c->fd == g_hd->fd) {
		g_hd=c->next;
	} else {
		struct conn *e;
		for(e=g_hd; e->next!=NULL; e=e->next) {
			if(e->next->fd==c->fd) {
				e->next=c->next;
				break;
			}
		}
	}

#ifdef GC_VERBOSE
	fprintf(stderr, "remove_conn(), after:\n");
	list_conn();
#endif
}

void accept_new(int epollfd, int s) {
	int cl_sock=accept(s, NULL, NULL);
	fcntl(cl_sock,F_SETFL,O_NONBLOCK);
	if(cl_sock<0) { perror("accept"); return; }
	struct conn *cc=malloc(sizeof(struct conn));
	if(cc==NULL) { fprintf(stderr, "malloc() failed\n"); close(cl_sock); shutdown(cl_sock, SHUT_RDWR); return; }
	bzero(cc, sizeof(struct conn));
	cc->fd=cl_sock;
	cc->created /*=cc->last_seen*/ = time(NULL);
	struct epoll_event ev;
	ev.events=EPOLLMASK;
	ev.data.ptr=(void*)cc;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, cl_sock, &ev)<0) { perror("epoll_ctl"); close(cl_sock); shutdown(cl_sock, SHUT_RDWR); return; }
#ifdef TLS
	cc->ssl=SSL_new(sslctx);
	SSL_set_fd(cc->ssl, cl_sock);
	SSL_set_accept_state(cc->ssl);
#endif
	insert_conn(cc);
}

void destroy_half_conn(struct conn *c) {
	if(c==NULL) return;
	// remove from remaining of event queue
	for(int i=curevent+1; i<nevents; i++)
		if(events[i].data.ptr==c)
			events[i].data.ptr=NULL;
#ifdef TLS
	if(c->ssl) {
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl=NULL;
	}
#endif
	if(c->fd > 0) {
		shutdown(c->fd, SHUT_RDWR);
		close(c->fd);
	}
	if(c->other)
		c->other->other=NULL;
	remove_conn(c);
	free(c);
}

void destroy_conn(struct conn* c) {
	if(c==NULL) return;
	if(c->other!=NULL) destroy_half_conn(c->other);
	c->other=NULL;
	destroy_half_conn(c);
}

int isspc(char c) {
	return(c==' '||c=='\r'||c=='\n');
}

int try_parse_appname(struct conn* c, char *appname, char *translated_req, int *translated_req_length) {
	// return -1 -> keep reading
	// -2 -> bad request
	// 0 -> appname found (in appname, translated_req, appname empty for '/')
	assert(c->sure_bind!=-1); 

	//printf("try_parse_appname() reqsz=%d req=%s\n", c->reqsz, c->req);

	int i=0;
	bzero(translated_req, REQSZ); 
	bzero(appname, APPNAMESZ);
	while(i<c->reqsz && i<REQSZ && isspc(c->req[i])) i++;
	while(i<c->reqsz && i<REQSZ && !isspc(c->req[i])) {
		translated_req[i]=c->req[i];
		i++;
	}
	int tr_req_index=i;
	while(i<c->reqsz && i<REQSZ && isspc(c->req[i])) i++;
	if(i==c->reqsz) return(-1);
	if(i==REQSZ || c->req[i]!='/') return(-2);
	i++;
	if(i==REQSZ) return(-2);
	translated_req[tr_req_index++]=' ';
//	translated_req[tr_req_index++]='/';
	int j=0;
	while(i<c->reqsz && i<REQSZ && j<REQSZ && !isspc(c->req[i]) && c->req[i]!='/') {
		appname[j++]=c->req[i++];
	}
	if(j<REQSZ)
		appname[j]=0;
	else
		return(-2);
	while(i<c->reqsz && i<REQSZ && tr_req_index<REQSZ) 
		translated_req[tr_req_index++]=c->req[i++];
	*translated_req_length=tr_req_index;
	//fprintf(stderr, "appname=%s translated_req=", appname);
	//write(STDERR_FILENO, translated_req, *translated_req_length);
	return(0);
}


int bind_localsock(struct conn *c, char *appname, char *local_socket_path, int epollfd) {
	if(c->sure_bind!=0) return(0);
	if(c->other!=NULL && strncmp(appname, c->other->req, APPNAMESZ)==0) return(0); // already bound to the right local socket
	if(c->other!=NULL && c->other->fd > 0) {
		// unbind 
		close(c->other->fd);
		shutdown(c->other->fd, SHUT_RDWR);
	} else {
		c->other=malloc(sizeof(struct conn));
		if(c->other==NULL) {
			perror("malloc");
			return(-1);
		}
		c->other->other=c;
		c->other->created= /* c->other->last_seen=*/ time(NULL);
		c->other->sure_bind=-1;
#ifdef TLS
		c->other->ssl=NULL;
#endif
		insert_conn(c->other);
	}
	c->other->fd=-1;
	strlcpy(c->other->req, appname, APPNAMESZ);
	c->sure_bind=1;
	c->other->fd=socket(AF_UNIX, SOCK_STREAM, 0);
	if(c->other->fd < 0) { 
		perror("socket"); 
		return(-1); 
	}
	struct sockaddr_un saddr;
	bzero(&saddr, sizeof(struct sockaddr_un));
	saddr.sun_family=AF_UNIX;
	strlcpy(saddr.sun_path, local_socket_path, 108);
	if(connect(c->other->fd, (struct sockaddr*)&saddr, sizeof(struct sockaddr_un))<0) {
		perror("connect");
		return(-1);
	}
	struct epoll_event ev;
	ev.events=EPOLLMASK;
	ev.data.ptr=(void*)(c->other);
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, c->other->fd, &ev)<0) {
		perror("epoll_ctl");
		return(-1);
	}
	return(0);
}

int search_bind_localsock(struct conn *c, char *appname, int argc, char **argv, int epollfd) {
	if(c->sure_bind!=0) return(0);
	// ret -2 -> appname not found
	// ret -1 -> sys error
	int n=0;
	while(n<argc) {
		int nn;
		for(nn=0; nn<strlen(appname) && nn<strlen(argv[n]) && nn<APPNAMESZ && argv[n][nn]!=':' && appname[nn]==argv[n][nn]; nn++) ;
		if(nn==strlen(appname) && nn<strlen(argv[n]) && argv[n][nn]==':') 
			return(bind_localsock(c, appname, argv[n]+nn+1, epollfd));
		n++;
	}
	return(-2);
}

void garbage_collect(void) {
	if(g_hd==NULL) {
		fprintf(stderr, "gc: no conn\n");
		return;
	}

	time_t now=time(NULL);
	struct conn *e, *en; 

#ifdef GC_VERBOSE
	fprintf(stderr, "before gc:\n");
	list_conn();
#endif

	for(e=g_hd; e!=NULL; ) {
		if(now - e->created > GC_TIMEOUT) {
#ifdef GC_VERBOSE
			fprintf(stderr, "   killing connection on fd %d\n", e->fd);
#endif
			en=e->next;
			destroy_half_conn(e);
			e=en;
		} else {
			e=e->next;
		}
	}

#ifdef GC_VERBOSE
	fprintf(stderr, "after gc:\n");
	list_conn();
#endif
}

void readall(struct conn *c) {
	char buf[4096];
#ifdef TLS
	while(SSL_read(c->ssl, buf, 4096)>0);
#else
	while(recv(c->fd, buf, 4096, MSG_DONTWAIT)>0);
#endif
}

void writefile(struct conn *c, int fd, int size) {
#ifdef TLS
	char buf[4096];
	int nr, ret;
	loop:
	while((nr=read(fd, buf, 4096))>0) {
		if((ret=SSL_write(c->ssl, buf, nr))<0) {
			int err=SSL_get_error(c->ssl, ret);
			if(err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) goto loop;
			printf("writefile() SSL_write error %d\n", err);
			destroy_conn(c);
		}
	}
#else
	sendfile(c->fd, fd, 0, size);
#endif
}

void writestr(struct conn *c, char *string) {
#ifdef TLS
	int ret;
	loop:
	if((ret=SSL_write(c->ssl, string, strlen(string)))<0) {
		int err=SSL_get_error(c->ssl, ret);
		if(err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) goto loop;
		printf("writestr() SSL_write error %d\n", err);
	}

#else
	write(c->fd, string, strlen(string));
#endif
}

int try_serve_cache(struct conn *c, char *appname, char *translated_req, int translated_req_len) {
	// 0->cache HIT
	// -1->cache MISS
	if(translated_req_len<6) return(-1);
	if(memcmp(translated_req,"GET /",5)==0 && isspc(translated_req[5])) {
		char buf[256];
		snprintf(buf, 256, "/tmp/cache/%s.html.gz", appname);
		struct stat sbuf;
		//printf("translated_req\n");write(STDERR_FILENO,translated_req,translated_req_len);
		if(stat(buf,&sbuf)==0 && sbuf.st_size>1 && access(buf,R_OK)==0 && \
					memmem(translated_req,translated_req_len,"gzip",4)!=NULL) {
			int fd=open(buf,O_RDONLY);
			snprintf(buf, 256, "HTTP/1.1 200\r\nContent-type: text/html;charset=utf8\r\nContent-encoding: gzip\r\nContent-length: %ld\r\n\r\n", sbuf.st_size);
			writestr(c, buf);
			writefile(c, fd, sbuf.st_size);
			close(fd);
			return(0);
		}
		snprintf(buf, 256, "/tmp/cache/%s.html", appname);
		if(stat(buf,&sbuf)==0 && sbuf.st_size>1 && access(buf,R_OK)==0) {
			int fd=open(buf,O_RDONLY);
			snprintf(buf, 256, "HTTP/1.1 200\r\nContent-type: text/html;charset=utf8\r\nContent-length: %ld\r\n\r\n", sbuf.st_size);
			writestr(c, buf);
			writefile(c, fd, sbuf.st_size);
			close(fd);
			return(0);
		}
	}
	return(-1);
}
int main(int argc, char **argv) {
#ifdef TLS
	if(argc<2 || !getenv("TLSCERT") || !getenv("TLSKEY")) {
		fprintf(stderr, "Usage: TLSCERT=<cert_pem> TLSKEY=<key_pem> %s <port> [<app_name>:<app_local_socket> [...]]\n", argv[0]);
		exit(1);
	}
#else
	if(argc<2) {
		fprintf(stderr, "Usage: %s <port> [<app_name>:<app_local_socket> [...]]\n", argv[0]);
		exit(1);
	}
#endif

#ifdef TLS
	sslctx=SSL_CTX_new(TLS_server_method());
	SSL_CTX_set_min_proto_version(sslctx, TLS1_2_VERSION);
	SSL_CTX_set_options(sslctx, SSL_OP_CIPHER_SERVER_PREFERENCE|SSL_OP_PRIORITIZE_CHACHA);
	SSL_CTX_set_cipher_list(sslctx, "HIGH");
	if(SSL_CTX_use_certificate_chain_file(sslctx, getenv("TLSCERT")) != 1) {
		printf("SSL_CTX_use_certificate_chain_file error\n");
		exit(1);
	}
	if(SSL_CTX_use_PrivateKey_file(sslctx, getenv("TLSKEY"), SSL_FILETYPE_PEM) != 1) {
		printf("SSL_CTX_use_PrivateKey_file error\n");
		exit(1);
	}
#endif
	int s=listen_sock((uint16_t)atoi(argv[1]));
	struct epoll_event ev;
	int epollfd=epoll_create(1);
	if(epollfd<0) { perror("epoll_create"); exit(1); }
	ev.events=EPOLLMASK;
	ev.data.fd=s;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, s, &ev)<0) {
		perror("epoll_ctl");
		exit(1);
	}
	int timerfd=timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	struct itimerspec itmsp;
	itmsp.it_interval.tv_sec=GC_INTERVAL;
	itmsp.it_value.tv_sec=GC_INTERVAL;
	timerfd_settime(timerfd, 0, &itmsp, NULL);
	ev.data.fd=timerfd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, timerfd, &ev)<0) {
		perror("epoll_ctl");
		exit(1);
	} 
	while(1) {
		int wstatus;
		waitpid(-1, &wstatus, WNOHANG);
		nevents=epoll_wait(epollfd, events, MAXEVENTS, -1);
		if(nevents<0) { perror("epoll_wait"); exit(1); }
		for(curevent=0; curevent<nevents; curevent++) {
			if(events[curevent].data.fd==s) {
				accept_new(epollfd, s);
			} else if(events[curevent].data.fd==timerfd) {
				uint64_t junk;
				read(timerfd, &junk, 8);
				garbage_collect();
			} else {
				struct conn *cc=(struct conn*)(events[curevent].data.ptr);
				// destroy_conn() can be called and destroy a conn still in events
				// this event will be marked as data.ptr==NULL
				// else we need to set MAXEVENTS to 1
				if(cc==NULL)
					continue;
				if(events[curevent].events != EPOLLIN) {
					//fprintf(stderr, "event not EPOLLIN\n");
					destroy_conn(cc);
					continue;
				}
				//cc->last_seen=time(NULL);
				char buf[4096];
				char appname[APPNAMESZ], translated_req[REQSZ];
				int nr, rc, translated_req_length;
				if(cc->sure_bind==0) {
					// read from remote, not bound (or unsure) to local socket
#ifdef TLS
					if((nr=SSL_read(cc->ssl, cc->req+cc->reqsz, REQSZ-cc->reqsz))<=0) {
						int err=SSL_get_error(cc->ssl, nr);
						if((err != SSL_ERROR_WANT_READ) && (err != SSL_ERROR_WANT_WRITE)) {
							printf("SSL_read error %d\n", err);
							destroy_conn(cc);
						}
						continue;
					}
					//printf("cc->req : %s\n", cc->req);
#else
					if((nr=recv(cc->fd, cc->req+cc->reqsz, REQSZ-cc->reqsz, MSG_DONTWAIT))<=0) {
						perror("recv");
						destroy_conn(cc);
						continue;
					}
#endif
					cc->reqsz+=nr;
					rc=try_parse_appname(cc, appname, translated_req, &translated_req_length);
					if(rc==-2) {
						writestr(cc, ERRMSG);
						destroy_conn(cc);
					//	fprintf(stderr, "Bad request\n");
						continue;
					} else if(rc==0) {
						cc->reqsz=0;
						bzero(cc->req, REQSZ);
						if(try_serve_cache(cc, appname, translated_req, translated_req_length)==0) {
							readall(cc);
							continue;
						} else if(strlen(appname)==0) {
							readall(cc);
							if(access(HOME,R_OK)!=0) {
								char page[]="HTTP/1.1 200\r\nContent-length: 4\r\n\r\nHome";
								writestr(cc, page);
							} else {
								struct stat stbuf;
								stat(HOME,&stbuf);
								char buf[256];
								snprintf(buf,256,"HTTP/1.1 200\r\nContent-type: text/html;charset=utf8\r\nContent-length: %d\r\n\r\n", (int)stbuf.st_size);
								writestr(cc, buf);
								int fd=open(HOME, O_RDONLY);
								writefile(cc, fd, stbuf.st_size);
								close(fd);
								continue;
							}
							continue;
						} else {
							rc=search_bind_localsock(cc, appname, argc, argv, epollfd);
							if(rc==-2) {
								readall(cc);
							//	fprintf(stderr, "Not found\n");
								writestr(cc, NOTFOUNDMSG);
								continue;
							} if(rc==-1) {
								readall(cc);
								writestr(cc, BOOMSG);
								destroy_conn(cc);
								continue;
							} else {
								if(write(cc->other->fd, translated_req, translated_req_length)<0) {
						//			perror("write translated_req");
									err:
									writestr(cc, BOOMSG);
									destroy_conn(cc);
									continue;
								}
#ifdef TLS
								while((nr=SSL_read(cc->ssl, buf, 4096))>0) {
#else
								while((nr=recv(cc->fd, buf, 4096, MSG_DONTWAIT))>0) {
#endif
									if(write(cc->other->fd, buf, nr)<0) {
										perror("write");
										goto err;
									}
								}
							}
						}
					}
				} else {
					// read from local or remote-bound
					assert(cc->other!=NULL);
#ifdef TLS
					if(cc->ssl) { // read from remote
						if((nr=SSL_read(cc->ssl, buf, 4096))<=0) {
							int err=SSL_get_error(cc->ssl, nr);
							if((err!=SSL_ERROR_WANT_READ)&&(err!=SSL_ERROR_WANT_WRITE)) {
								printf("SSL_read_error %d\n", err);
								destroy_conn(cc);
							}
							continue;
						}
					} else { // read from local
						if((nr=recv(cc->fd, buf, 4096, O_NONBLOCK))<=0) {
							if(nr<0) perror("recv");
							destroy_conn(cc);
							continue;
						}
					}
#else
					if((nr=recv(cc->fd, buf, 4096, O_NONBLOCK))<=0) {
						//if(nr<0) perror("recv");
						destroy_conn(cc);
						continue;
					}
#endif
					if(cc->sure_bind==-1) {
						// read from local, watch for switching protocol
						if(cc->other->sure_bind!=2) {
							if(memcmp(buf,"HTTP/1.1 101",12)==0) {
								//fprintf(stderr, "Protocol switched\n");
								cc->other->sure_bind=2;
							}
						}
						if(cc->other->sure_bind!=2) {
							// except switched protocol, always translate after a local reply
							cc->other->sure_bind=0; 
						}
					}
#ifdef TLS
					if(cc->other->ssl==NULL) { // write to local
						if((send(cc->other->fd, buf, nr, O_NONBLOCK))<0) {
							perror("send");
							destroy_conn(cc);
							continue;
						}
					} else { // write to remote
						int ret;
						loop:
						if((ret=SSL_write(cc->other->ssl, buf, nr))<0) {
							int err=SSL_get_error(cc->other->ssl, ret);
							if(err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE) goto loop;
							printf("SSL_write error %d\n", err);
							destroy_conn(cc);
							continue;
						}
					}
#else
					if((send(cc->other->fd, buf, nr, O_NONBLOCK))<0) {
						//perror("send");
						destroy_conn(cc);
						continue;
					}
#endif
				} 
			}
		}
	}
}
