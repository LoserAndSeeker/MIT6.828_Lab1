#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define PORT 80
#define VERSION "0.1"
#define HTTP_VERSION "1.0"

#define E_BAD_REQ	1000

#define BUFFSIZE 512
#define MAXPENDING 5	// Max connection requests

struct http_request {
	int sock;
	char *url;
	char *version;
};

struct responce_header {
	int code;
	char *header;
};

struct responce_header headers[] = {
	{ 200, 	"HTTP/" HTTP_VERSION " 200 OK\r\n"
		"Server: jhttpd/" VERSION "\r\n"},
	{0, 0},
};

struct error_messages {
	int code;
	char *msg;
};

struct error_messages errors[] = {
	{400, "Bad Request"},
	{404, "Not Found"},
};

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

static void
req_free(struct http_request *req)
{
	free(req->url);
	free(req->version);
}

static int
send_header(struct http_request *req, int code)
{
	struct responce_header *h = headers;
	while (h->code != 0 && h->header!= 0) {
		if (h->code == code)
			break;
		h++;
	}

	if (h->code == 0)
		return -1;

	int len = strlen(h->header);
	if (write(req->sock, h->header, len) != len) {
		die("Failed to send bytes to client");
	}

	return 0;
}

static int
send_data(struct http_request *req, int fd)
{
	// LAB 6: Your code here.
	int n;
	char buf[BUFFSIZE]; //不懂为啥，写PGSIZE就会出现页面错误，太大了？
	while((n = read(fd, buf, (long)sizeof(buf)))>0){
		if(write(req->sock, buf, n) != n)
			die("Failed to send file to client");
	}	
	return n;	
	panic("send_data not implemented");

}

static int
send_size(struct http_request *req, off_t size)
{
	char buf[64];
	int r;

	r = snprintf(buf, 64, "Content-Length: %ld\r\n", (long)size);
	if (r > 63)
		panic("buffer too small!");

	if (write(req->sock, buf, r) != r)
		return -1;

	return 0;
}

static const char*
mime_type(const char *file)
{
	//TODO: for now only a single mime type
	return "text/html";
}

static int
send_content_type(struct http_request *req)
{
	char buf[128];
	int r;
	const char *type;

	type = mime_type(req->url);
	if (!type)
		return -1;

	r = snprintf(buf, 128, "Content-Type: %s\r\n", type);
	if (r > 127)
		panic("buffer too small!");

	if (write(req->sock, buf, r) != r)
		return -1;

	return 0;
}

static int
send_header_fin(struct http_request *req)
{
	const char *fin = "\r\n";
	int fin_len = strlen(fin);

	if (write(req->sock, fin, fin_len) != fin_len)
		return -1;

	return 0;
}

// given a request, this function creates a struct http_request
static int
http_request_parse(struct http_request *req, char *request)
{
	const char *url;
	const char *version;
	int url_len, version_len;

	if (!req)
		return -1;

	if (strncmp(request, "GET ", 4) != 0)
		return -E_BAD_REQ;

	// skip GET
	request += 4;

	// get the url
	url = request;
	while (*request && *request != ' ')
		request++;
	url_len = request - url;

	req->url = malloc(url_len + 1);
	memmove(req->url, url, url_len);
	req->url[url_len] = '\0';

	// skip space
	request++;

	version = request;
	while (*request && *request != '\n')
		request++;
	version_len = request - version;

	req->version = malloc(version_len + 1);
	memmove(req->version, version, version_len);
	req->version[version_len] = '\0';

	// no entity parsing

	return 0;
}

static int
send_error(struct http_request *req, int code)
{
	char buf[512];
	int r;

	struct error_messages *e = errors;
	while (e->code != 0 && e->msg != 0) {
		if (e->code == code)
			break;
		e++;
	}

	if (e->code == 0)
		return -1;

	r = snprintf(buf, 512, "HTTP/" HTTP_VERSION" %d %s\r\n"
			       "Server: jhttpd/" VERSION "\r\n"
			       "Connection: close"
			       "Content-type: text/html\r\n"
			       "\r\n"
			       "<html><body><p>%d - %s</p></body></html>\r\n",
			       e->code, e->msg, e->code, e->msg);

	if (write(req->sock, buf, r) != r)
		return -1;

	return 0;
}

static int
send_file(struct http_request *req)
{
	int r;
	off_t file_size = -1;
	int fd;

	// open the requested url for reading
	// if the file does not exist, send a 404 error using send_error
	// if the file is a directory, send a 404 error using send_error
	// set file_size to the size of the file

	// LAB 6: Your code here.
	//panic("send_file not implemented");
	struct Stat st;
	//cprintf("hello send file\n");
	/*if ((r = fd_lookup(req->sock, &sfd))<0) //我还以为open的模式应该是sfd->fd_omode
		return send_error(req, 404); */
	if ((fd = open(req->url, O_RDONLY)) < 0) //尼吗，就一个括号扩错地方了，让我找了两个小时bug？？？
		return send_error(req, 404);
	if ((r = fstat(fd, &st)) < 0){
		r = send_error(req, 404);
		cprintf("hello fstat\n");
		goto end;
	}
	//cprintf("hello isdir %d\n", st.st_isdir);
	if (st.st_isdir){
		r =  send_error(req, 404);
		goto end;
	}
	file_size = st.st_size;


	if ((r = send_header(req, 200)) < 0)
		goto end;

	if ((r = send_size(req, file_size)) < 0)
		goto end;

	if ((r = send_content_type(req)) < 0)
		goto end;

	if ((r = send_header_fin(req)) < 0)
		goto end;

	r = send_data(req, fd);

end:
	close(fd);
	return r;
}

static void
handle_client(int sock)
{
	struct http_request con_d;
	int r;
	char buffer[BUFFSIZE];
	int received = -1;
	struct http_request *req = &con_d;

	while (1)
	{
		// Receive message
		if ((received = read(sock, buffer, BUFFSIZE)) < 0)
			panic("failed to read");

		memset(req, 0, sizeof(*req));

		req->sock = sock;

		r = http_request_parse(req, buffer); //从buffer中解析出req的三个字段的填充信息
		if (r == -E_BAD_REQ)
			send_error(req, 400);
		else if (r < 0)
			panic("parse failed");
		else
			send_file(req);

		req_free(req);

		// no keep alive
		break;
	}

	close(sock); //此处关闭的是accept自动创建的与客户端建立了连接的真正服务器端套接字
}

/* 补充一点客户端知识
客户端socket()创建socket后立即调用connet(),而没有经过bind()函数，因为在调用connet()时内核中会自动分配主机IP和一个随机端口
connet()函数获得返回值的条件是：服务器端接收连接请求或者发生断网等异常而中断连接请求。此时的接收连接只是被加入了连接请求等待队列，并未被accept

*/

/*I/O缓冲即输入输出缓冲
read()跟write函数都不是直接发送数据，而是借助I/o缓冲。write会把数据移到输出缓冲，read则从输入缓冲读取数据
I/O缓冲在每个TCP套接字中单独存在，在创建套接字时会自动生成
即使关闭套接字也会继续传递输出缓冲中的数据，但是关闭套接字会丢失输入缓冲中的数据
*/

void
umain(int argc, char **argv)
{
	int serversock, clientsock;
	struct sockaddr_in server, client; //_in代表internet，sockaddr_in对于ipv4

	binaryname = "jhttpd";

	/*Socket的出现 只是使得程序员更方便地使用TCP/IP协议栈而已，是对TCP/IP协议的抽象，
	从而形成了我们知道的一些最基本的函数接口，比如create、listen、connect、accept、send、read和write等等。*/		

	// Create the TCP socket
	// PF_INET=2(domain协议域：ipv4+端口号), SOCK_STREAM=1(TCP协议), IPPROTO_TCP=6
	// serversock是分配给sockid对应的socket的文件描述符num，其中sockid是通过ipc通信由nsenv给出
	if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		die("Failed to create socket");

	// Construct the server sockaddr_in structure
	memset(&server, 0, sizeof(server));		// Clear struct
	server.sin_family = AF_INET;			// Internet/IP  	AF_INET=2
	server.sin_addr.s_addr = htonl(INADDR_ANY);	// IP address 利用INADDR_ANY可自动获取运行服务器端的计算机IP地址
	server.sin_port = htons(PORT);			// server port		PORT=80?

	// Bind the server socket
	// 由服务端调用，把一个地址族中的特定地址和 socket 联系起来
	if (bind(serversock, (struct sockaddr *) &server,
		 sizeof(server)) < 0)
	{
		die("Failed to bind the server socket");
	}

	// Listen on the server socket
	//服务器socket相当于门卫一样，把到来的连接请求放到连接请求等待队列
	//此时的serversock仅仅起门卫作用，并不是真正的服务器端socket，下面的clientsock才是与客户端建立了连接的服务器端socket
	if (listen(serversock, MAXPENDING) < 0) 
		die("Failed to listen on server socket");

	cprintf("Waiting for http connections...\n");

	while (1) {
		// 这样的实现很简单，一次只能服务一个客户端，因为还没有使用线程概念
		unsigned int clientlen = sizeof(client);
		// Wait for client connection
		// accept从等待队列的队头取出1个连接请求，自动创建一个新套接字并完成连接请求
		// 返回的是accept创建的套接字描述符，所以无论serversock还是clientsock都是服务器端的socket
		// 第二个参数保存着发起连接请求的客户端地址信息
		if ((clientsock = accept(serversock, 
					 (struct sockaddr *) &client,
					 &clientlen)) < 0) 
		{
			die("Failed to accept client connection");
		}
		handle_client(clientsock);
	}

	close(serversock);
}
