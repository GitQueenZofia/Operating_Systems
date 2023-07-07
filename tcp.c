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
#define MAX_BUF 100
#define DATE 21

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

int bind_tcp_socket(uint16_t port)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(AF_INET, SOCK_STREAM);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    //addr.sin_addr.s_addr = inet_addr(INADDR_ANY);
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
    fcntl(nfd, F_SETFL, fcntl(nfd, F_GETFL) | O_NONBLOCK);
	return nfd;
}


void usage(char *name)
{
	fprintf(stderr, "USAGE: %s port file\n", name);
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
	char*busy="BUSY";
	for(int i=0;i<CLIENTS;i++){
		if(hosts[i]==-1){
			idx=i;
			break;
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
		if(TEMP_FAILURE_RETRY(close(cfd))<0)
			ERR("close");
	}
	
}
void process(char*buf,int* n,int file,int fd,fd_set* base_rfds,fd_set*rfds,int*hosts)
{
	time_t timestamp =time(NULL);
	char t[DATE];
	memset(t,0,DATE);
	snprintf(t,sizeof(t),"[%ld] ",timestamp);
	
	char new[MAX_BUF+DATE+1];
	memset(new,0,MAX_BUF+DATE+1);

	snprintf(new,sizeof(new),"%s%s\n",t,buf);

	printf("writing to a file: %s",new);

	if(bulk_write(file,new,strlen(new))<0){
			if(errno==EPIPE){
				lost(fd,hosts,base_rfds,rfds);
				return;
			}
            ERR("write");
        }
	
}


void communicate(int fd, int* hosts, fd_set* base_rfds, fd_set* rfds,int file)
{
    ssize_t size,total=0;
    char msg[MAX_BUF];
	int n;
	memset(msg,0,MAX_BUF);
	while(strchr(msg,'\n')==NULL&&total<MAX_BUF)
    {
        if((size=read(fd,msg+total,MAX_BUF-total))<0){
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            ERR("read:");
        }
        if(size==0)
        {
            lost(fd,hosts,base_rfds,rfds);
            return;
        }
        total+=size;
    }
	n=total;
    char*next=msg;
	char* fragment=strchr(msg,'\n');
	while(fragment!=NULL)
	{
		*fragment=0;
		process(next,&n,file,fd,base_rfds,rfds,hosts);
		next=fragment+1;
		fragment=strchr(fragment+1,'\n');
	}
}

void doServer(int fdT, int*hosts,int file)
{
	int cfd, fdmax=fdT;
	fd_set base_rfds, rfds;
	sigset_t mask, oldmask;
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	FD_SET(STDIN_FILENO,&base_rfds);
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
	if (argc != 3) {
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
	if((file=open(argv[2],O_WRONLY|O_CREAT,0777))<0){
        ERR("open");
    }
	fdT = bind_tcp_socket(atoi(argv[1]));
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);

	doServer(fdT,hosts,file);
	for(int i=0;i<CLIENTS;i++){
		if(hosts[i]>0&&TEMP_FAILURE_RETRY(close(hosts[i]))<0){
        ERR("close");
        }
	}
	if (TEMP_FAILURE_RETRY(close(file)) < 0)
		ERR("close");
	if (TEMP_FAILURE_RETRY(close(fdT)) < 0)
		ERR("close");
	fprintf(stderr, "Server has terminated.\n");
	return EXIT_SUCCESS;
}