#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define BACKLOG 3
#define CLIENTS 2
#define MAX_BUF 128

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig)
{
	do_work = 0;
}

int sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

int make_socket(int domain, int type)
{
	int sock;
	sock = socket(domain, type, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

int bind_tcp_socket(uint16_t port, char* address)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(AF_INET, SOCK_STREAM);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr=htonl(INADDR_ANY);

    if(addr.sin_addr.s_addr < 0)
        ERR("addr");

    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    if (listen(socketfd, BACKLOG) < 0)
        ERR("listen");
    return socketfd;
}

int add_new_client(int sfd)
{
	int nfd;
	if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno)
			return -1;
		ERR("accept");
	}
	return nfd;
}


void usage(char *name)
{
	fprintf(stderr, "USAGE: %s file socket port \n", name);
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
		if (0 == c)
			return len;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(write(fd, buf, count));
		if (c < 0)
			return c;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

void lost(int fd,int* hosts,fd_set*base_rfds,fd_set*rfds)
{
    for(int i=0;i<CLIENTS;i++)
    {
        if(hosts[i]==fd)
        {
            printf("lost connection with host number %d\n",i);
            hosts[i]=-1;
            FD_CLR(fd,base_rfds);
            FD_CLR(fd,rfds);
        }
    }
}

void findIndex(int cfd,int*fdmax,int*hosts,fd_set*base_rfds)
{
	int idx=-1;
	char*busy="4BUSY";
	for(int i=0;i<CLIENTS;i++){
		if(hosts[i]==-1){
			idx=i;
		}
	}
	if(idx>=0){
		hosts[idx]=cfd;
		FD_SET(cfd,base_rfds);
		*fdmax=*fdmax>cfd+1?*fdmax:cfd+1;
	}
	else{
		if(TEMP_FAILURE_RETRY(bulk_write(cfd,busy,strlen(busy)))<0){
			ERR("write");
		}
	}
	
}
void process(char*buf,int* n)
{
	buf[*n]='\0';
	time_t currentTime = time(NULL);
    struct tm* timeInfo = localtime(&currentTime);
    char timeString[9];
    strftime(timeString, sizeof(timeString), "%H:%M:%S", timeInfo);

    int timeStringLength = strlen(timeString);
    memmove(buf + timeStringLength, buf, *n + 1);
    memcpy(buf, timeString, timeStringLength);
	*n=*n+timeStringLength;

}
void process1(char*buf,int n)
{
	for(int i=0;i<n;i++)
	{ if(buf[i]!=tolower(buf[i]))
		buf[i]=tolower(buf[i]);
	else if(buf[i]!=toupper(buf[i]))
		buf[i]=toupper(buf[i]);
	}
}
void process2(char*buf,int n)
{
	for(int i=0;i<n;i++)
	{ if(buf[i]==' ') buf[i]='_';
        else if(buf[i]=='_') buf[i]=' ';
	}
}

void communicate(int fd, int* hosts, fd_set* base_rfds, fd_set* rfds, int file)
{
    int n,m;
    char buf[MAX_BUF+9];
	memset(buf,0,MAX_BUF+9);
	char*wrong="17WRONG FILTER MODE";
    char c[1],s[2];
    ssize_t size,total=0;
	if((size=read(fd,&c,sizeof(char)))<0){
        ERR("read");
    }
	m=c[0]-'0';
	if(size==0){
		lost(fd,hosts,base_rfds,rfds);
		return;
	}
	printf("m=%d\n",m);

	if(m<1||m>3){
		printf("wrong filter mode\n");
		if(write(fd,wrong,strlen(wrong))<0)
			ERR("write");
		char trash_message[MAX_BUF];
        if (read(fd, trash_message, MAX_BUF) < 0)
            ERR("read:");
		return;
	}
    if((size=read(fd,s,sizeof(char[2])))<0){
        ERR("read");
    }
	if(size==0){
		lost(fd,hosts,base_rfds,rfds);
		return;
	}
    if(s[0]>='0'&&s[1]>='0'&&s[0]<='9'&&s[1]<='9'){
        n=(s[0]-'0')*10+s[1]-'0';
        while((size=read(fd,buf+total,n+1-total))>0){
            total+=size;
			if(size==0){
				lost(fd,hosts,base_rfds,rfds);
				return;
			}
        }
		if(total>n+1)
		{
			printf("too big message\n");
			return;
		}
		if(m==1||m==3) process1(buf,n);
		if(m==2||m==3) process2(buf,n);
		process(buf,&n);
        if(bulk_write(file,buf,n)<0){
			if(errno==EPIPE){
				lost(fd,hosts,base_rfds,rfds);
				return;
			}
            ERR("write");
        }
    }
}

void doServer(int fdT, int*hosts, int file)
{
	int cfd, fdmax=fdT;
	fd_set base_rfds, rfds;
	sigset_t mask, oldmask;
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	while (do_work) {
		rfds = base_rfds;
		if (pselect(fdmax + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0) {
            if(FD_ISSET(fdT,&rfds)){
                printf("new add request\n");
                cfd=add_new_client(fdT);  
                if(cfd>=0){
					findIndex(cfd,&fdmax,hosts,&base_rfds);
				}               
            }
            for(int fd=0;fd<fdmax+1;fd++){
                if(FD_ISSET(fd,&rfds)&&fd!=fdT)
                	communicate(fd,hosts,&base_rfds,&rfds,file);     
			}
		}else {
			if (EINTR == errno)
				continue;
			ERR("pselect");
		}
	}
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char **argv)
{
	int fdT,file;
	int hosts[CLIENTS];
	int new_flags;
	if (argc != 4) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
    for(int i=0;i<CLIENTS;i++){
        hosts[i]=-1;
	}

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:");
	if (sethandler(sigint_handler, SIGINT))
		ERR("Seting SIGINT:");

	fdT = bind_tcp_socket(atoi(argv[3]),argv[2]);
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);

    if((file=open(argv[1],O_WRONLY|O_CREAT,0777))<0){
        ERR("open");
    }
	doServer(fdT,hosts,file);
	for(int i=0;i<CLIENTS;i++){
		if(hosts[i]>0&&TEMP_FAILURE_RETRY(close(hosts[i]))<0){
        ERR("close");
    }
	}
	if (TEMP_FAILURE_RETRY(close(fdT)) < 0)
		ERR("close");
	fprintf(stderr, "Server has terminated.\n");
	return EXIT_SUCCESS;
}
