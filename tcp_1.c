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

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define BACKLOG 3
#define CLIENTS 8
#define MAX 128

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
	fprintf(stderr, "USAGE: %s socket port\n", name);
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
            //FD_CLR(fd,wait);
        }
    }
}

void communicate(int cfd, int* hosts,fd_set* base_rfds,fd_set*rfds)
{
    ssize_t size,total=0;
    char msg[MAX];
    memset(msg,0,MAX);
    char* unknown="Unknown address!";
    while(strchr(msg,'$')==NULL&&total<MAX)
    {
        if((size=read(cfd,msg+total,MAX))<0){
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            ERR("read:");
        }
        if(size==0)
        {
            lost(cfd,hosts,base_rfds,rfds);
            return;
        }
        total+=size;
    }
    if(total<MAX)
    {
        *strchr(msg,'$')='\0';
        int dst=msg[1]-'0';
        msg[0]='0';
        // broadcast
        if(dst==9)
        {
            for(int i=0;i<CLIENTS;i++)
            {
                if(hosts[i]>0)
                {
                    //printf("host %d = %d\n",i,hosts[i]);
                    if(write(hosts[i],msg,strlen(msg))<0){
                        if(errno==EPIPE)
                            lost(hosts[i],hosts,base_rfds,rfds);
                        else ERR("write");
                    }
                }
            }
        }
        // normal client
        if(dst>0&&dst<=CLIENTS&&hosts[dst]>0)
        {
            if(bulk_write(hosts[dst],msg,strlen(msg))<0){
                    if(errno==EPIPE)
                        lost(hosts[dst],hosts,base_rfds,rfds);
                    else ERR("write");
                }
        }
        if(dst<0||dst>CLIENTS+1||hosts[dst]<0)
        {
            if(bulk_write(cfd,unknown,strlen(unknown))<0){
                    if(errno==EPIPE)
                        lost(cfd,hosts,base_rfds,rfds);
                    else ERR("write");
                }
        }

    }    
}

void assignAddress(int fd, int* hosts, int* max, fd_set* base_rfds, fd_set* rfds)
{
    ssize_t size=0;
    FD_CLR(fd,rfds);
    char n;
    char* wrong_address="Wrong address!";
    char* address_taken="Address taken!";
    while((size=TEMP_FAILURE_RETRY((size=read(fd,&n,1))))<0)
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        ERR("read");
    }
    if(size==1)
    {
        int k=n-'0';
        printf("request for address %d\n",k);
        if(k<0||k>CLIENTS+1)
        {
            if(bulk_write(fd,wrong_address,strlen(wrong_address))<0)
                ERR("write");
            if(TEMP_FAILURE_RETRY(close(fd))<0)
                ERR("close");
            return;
        }
        if(hosts[k]!=-1)
        {
            if(TEMP_FAILURE_RETRY(bulk_write(fd,address_taken,strlen(address_taken)))<0)
                ERR("write");         
            if(TEMP_FAILURE_RETRY(close(fd))<0)
                ERR("close");
            return;
        }
        hosts[k]=fd;
        if(fd>*max) *max=fd;
        FD_SET(fd, base_rfds);
        if(bulk_write(fd,&n,1)<0)
            ERR("write");       
        return;
    }
    if(size==0)
    {    
        lost(fd,hosts,base_rfds,rfds);
        return;
    }
    if(TEMP_FAILURE_RETRY(close(fd))<0)
        ERR("close");

}

void doServer(int fdT)
{
	int cfd, fdmax=fdT;
    int hosts[CLIENTS+1];
    for(int i=1;i<CLIENTS+1;i++){
        hosts[i]=-1;
    }
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
            if(FD_ISSET(fdT,&rfds))
            {
                printf("new add request\n");
                cfd=add_new_client(fdT);
                if(cfd>=0)
                    assignAddress(cfd,hosts,&fdmax,&base_rfds,&rfds);
            }
            for(int i=1;i<CLIENTS+1;i++)
            {
                if(FD_ISSET(hosts[i],&rfds))
                {
                    communicate(hosts[i],hosts,&base_rfds,&rfds);
                }
            }
		} else {
			if (EINTR == errno)
				continue;
			ERR("pselect");
		}
	}
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
    for(int i=1;i<CLIENTS+1;i++){
		if(hosts[i]>0&&TEMP_FAILURE_RETRY(close(hosts[i]))<0){
            ERR("close");
        }
    }
}

int main(int argc, char **argv)
{
	int fdT;
	int new_flags;
	if (argc != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:");
	if (sethandler(sigint_handler, SIGINT))
		ERR("Seting SIGINT:");

	fdT = bind_tcp_socket(atoi(argv[2]),argv[1]);
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);
	doServer(fdT);
	if (TEMP_FAILURE_RETRY(close(fdT)) < 0)
		ERR("close");
	fprintf(stderr, "Server has terminated.\n");
	return EXIT_SUCCESS;
}