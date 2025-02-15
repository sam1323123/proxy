#include <stdio.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"
#include <assert.h> 
#include <error.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define PORTLENGTH 10 

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char* VERSION = "HTTP/1.0" ;
//extern cache_t *Cache ; 


//#define DEBUG
#ifdef DEBUG
#define dbg_printf(s) (printf(s))
#else
#define dbg_printf(s) 
#endif

/*Defined function start */
void *client_thread(void* vargp);
void process_requesthdrs(rio_t *rio,int connfd ,int clientfd, char *host) ;
int parse_uri(char* uri, char* server, char* port, char*path) ;
int process_reply(int connfd, int clientfd, char*serv, char* path) ;
int error_handler(int fd) ;

/* Defined function end */

/*cehcks for ECONNRESET or EPIPE and closes the given fd*/
int error_handler(int fd) 
{
  if(fd != -1 && (errno == ECONNRESET || errno == EPIPE) ) 
    {
      Close(fd) ; //if fd is open ie fd != -1 
      errno = 0 ;
      return 1 ;
    }
  return 0; //no error detected

}


int main(int argc , char**argv)
{
  Signal(SIGPIPE, SIG_IGN) ; // ignore sigpipes 
  int listenfd, *connfdp ;
  socklen_t clientlen ;
  struct sockaddr_storage clientaddr ;
  pthread_t tid ;

  if(argc != 2) { //invalid arguments
    fprintf(stderr, "usage: %s <port>\n", argv[0]) ;
    exit(0) ;
  }

  listenfd = Open_listenfd(argv[1]) ; //open listenfd on port argv[1]
  cache_init() ; // initialize the cache

  while(1) {
    clientlen = sizeof(struct sockaddr_storage) ;
    connfdp = Malloc(sizeof(int)) ; //connfd is ptr to int
    //REMEMBER to FREE in thread
    *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen) ;
    Pthread_create(&tid , NULL, client_thread , connfdp) ;
  }
  Close(listenfd) ;
  cache_free() ;
    return 0;
}

/*parses the get request, connect to web server and delivers requested content*/
/*Objects are checked and fetched from cached if they exist */
/*handling of re/wr locks is done in this function */
void *client_thread(void *vargp){
  
  int connfd = *(int*)vargp ; 
  //obtain fd to client from vargp
  Pthread_detach(pthread_self()) ;
  Free(vargp) ; //free malloced ptr 
  char buf[MAXLINE] , method[MAXLINE], uri[MAXLINE], version[MAXLINE] ;
  char serv[MAXLINE] , path[MAXLINE] , port[PORTLENGTH];
  rio_t rio ;
  int clientfd = -1; //socket to web server
  Rio_readinitb(&rio, connfd) ;
  
  if(Rio_readlineb(&rio , buf, MAXLINE) >= 0) {
    //keeps connection alive until EOF detected
    dbg_printf(buf) ;// check http request
 
    sscanf(buf,"%s %s %s", method, uri, version) ;
    if(strcasecmp(method, "GET") )
      {
      // if not method get
	dbg_printf(method) ;
	dbg_printf("METHOD NOT IMPLEMENTED\n") ;
	printf( "Proxy does not implement this method\n") ;
	//Close(connfd) ; //close connection to client
	return NULL;
      //continue and wait for next request
      }
    int parse_success ;
    parse_success = parse_uri(uri,serv, port, path) ; 
    // get server name ,port and request path
    dbg_printf(serv) ; dbg_printf("THIS IS SERVER \n");
    dbg_printf(port) ; dbg_printf("THIS IS PORT \n"); 
 
    P(&(Cache->lock)) ;//get and insert cannot occur concurrently
    cache_line_t *cached = cache_get(serv, path) ;

    V(&(Cache->lock)) ; 
    if(cached != NULL) //object exists in cache
      {
	size_t s = cached->size ;

	Rio_writen(connfd, cached->object, s) ;
	// transfer to client 
	V(&(cached->lock)) ; 
	//release lock that was locked in cache_get 
	Close(connfd);
	return NULL;
      }
 
    if( parse_success && (clientfd = Open_clientfd(serv, port)) != -1 ) 
      //successful connection with web server after successful parse
      {// establish socket to server
	sprintf(buf, "%s %s %s\r\n" ,method, path, VERSION) ;

	dbg_printf("SEND "); dbg_printf(buf) ;

	Rio_writen(clientfd, buf, strlen(buf)) ; 
	if(error_handler(clientfd))
	  {
	    Close(connfd) ;
	    //close all sockets and exit current thread
	    return NULL;
	  }
	//strlen includes all characters including \n
      }
    else // unsuccessful connection
      {
	dbg_printf("FAILED TO CONNECT "); dbg_printf(buf) ;
	Close(connfd) ;      
	return NULL ; 
      }
    
    process_requesthdrs(&rio,connfd,clientfd, serv) ;
    /* finish sending request to web server */
    /*send reply from server to client */
    
    process_reply(connfd, clientfd, serv, path) ;
    //gets reply from server and transfers to client
  }
  Close(connfd) ; //close connection to client
  Close(clientfd) ;//close connection to web server 
  return NULL ;
}


/* obtain webserver, port num and path from client string */
int parse_uri(char* uri, char* server, char* port, char* path) 
{
  //char subPath[MAXLINE];
  char*bin ; 
  int prot = 10 ; // length to place http:// type strings
  char hd[prot] ;
  // int numRead ;
  dbg_printf("URI = ") ;
  dbg_printf(uri) ;
  char *start = uri ; // start address of uri
  bin = strstr(uri,"//") ;
  if(bin == NULL) return 0 ; //malformed uri
  strncpy(hd,uri,(bin-start)) ; //copy protocol into hd

  dbg_printf(hd) ; dbg_printf(" THIS IS hd\n") ;

  long length = strlen(hd)+ 1 ; //lenth of protocol + // 
  uri = start + length ;
  
  dbg_printf(uri) ; dbg_printf(" THIS IS uri\n");

  bin = strchr(uri, '/') ; //get start addr of path
  if(bin == NULL) strcpy(path, "/") ; //default path
  //else bin is at first / in uri 
  else strcpy(path, bin) ; //cpy path starting from / in url
  
  dbg_printf(path) ; dbg_printf(" THIS IS PATH\n"); 
 
  
  length = (unsigned long)bin ; //address at first /
  bin = strchr(uri, ':') ; //check if port is present
  if(bin != NULL) {
    //bin is at : in uri
    strncpy(server, uri, (bin-uri)) ; //copy IP into server
  
    dbg_printf(server) ; dbg_printf(" pServer \n"); 

    length = length - ((unsigned long)bin) -1; 
    //gets length of port by subtracting 2 addresses
    strncpy(port,(bin+1),length) ;     
  }  
  else {
    strncpy(server, uri, ((char*)length-uri)) ; //copy IP into server
  
    dbg_printf(server) ; dbg_printf(" pServer\n");
    strcpy(port, "80") ;
  }
  dbg_printf(port) ; dbg_printf("\n") ;
  
  return 1 ;
 
}

/* send required hdrs to web server and initiate reply */ 
/*GET request line already sent*/
//must we account for repeats of hdr? What about host name?  YES
//ALSO ask can we assume that readlineb will not get interrupted and 
void process_requesthdrs(rio_t *rio, int connfd,int clientfd, char *host)
{
  char buf[MAXLINE] ;// title[MAXLINE], data[MAXLINE];
  buf[0] = '0' ; 
  while(strcmp(buf, "\r\n")) //while not at hdr end
    {
      Rio_readlineb(rio, buf, MAXLINE) ; 
      
      Rio_writen(clientfd,buf, strlen(buf)) ;
      // write to web server request hdrs
      if(error_handler(clientfd))
	{
	  Close(connfd) ;
	  return ;
	}
    } 
    
    sprintf(buf, "Host: %s\r\n",host) ;
    Rio_writen(clientfd, buf, strlen(buf)) ;//write Host hdr
    Rio_writen(clientfd, (void*)user_agent_hdr, strlen(user_agent_hdr)) ;
    //write user-agent 
    
    char* line =  "Connection: close\r\n" ;
    Rio_writen(clientfd, line, strlen(line) ) ;
    line = "Proxy-Connection: close\r\n" ;
    Rio_writen(clientfd, line, strlen(line)) ;
    line = "\r\n" ;
    Rio_writen(clientfd, line, strlen(line)) ;
    if(error_handler(clientfd))
	{
	  Close(connfd) ;
	  return ;
	}
    return ;
}

/* fetched data from web server and delivers it to the client. A buffer is
   generated to hold the content and cached if the size fits the requirements
re/wr locks are also handled here */

int process_reply(int connfd, int clientfd, char *serv, char *path)
{
  rio_t rio ;
  char buf[MAXBUF] ;
  int r ;
  
  char *obj = Malloc(MAX_OBJECT_SIZE) ; 
  //obj freed in cache functions if cached else, free here
  size_t count = 0 ;
  int c = 0 ; //bool to see if max obj size exceeded
  
  Rio_readinitb(&rio, clientfd) ;
  while((r=Rio_readnb(&rio, buf, MAXBUF)) != 0)
    {// while not at EOF
      if(r == -1) return 0 ; //read error
      
      if(count+r <= MAX_OBJECT_SIZE)
	{
	  char *i = obj + count ;
	  memcpy((void*)(i), buf, r) ;
	  //copy r bytes from buf to obj starting from next available point
	  count+=r ;
	}
      else 
	{
	  c = 1 ; // don't cache
	  count += r ;
	}
      Rio_writen(connfd, buf, r) ; //serves content to client
      if(error_handler(clientfd))
	{
	  Close(connfd) ;
	  return 0;
	}
    }

  if(c == 0) //cache obj
    {
      P(&(Cache->lock)) ; //lock Cache so only 1 insert/evict at a time
      
      //lock on individual cache line will prevent eviction of line that is 
      // currently being used by another thread
    
      cache_insert(obj, count, serv,path) ;
    
      V(&(Cache->lock)) ;
     
    }
  else if(c == 1) free(obj) ; 
  return 1 ;
    
}

