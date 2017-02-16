/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
// #include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(int client) {
  char buf[1024];
  size_t numchars;
  char method[255];
  char url[255];
  char path[512];
  size_t i, j;
  /*
  stat()用来将参数file_name 所指的文件状态, 复制到参数buf 所指的结构中。

  struct stat
  {
      dev_t st_dev; //device 文件的设备编号
      ino_t st_ino; //inode 文件的i-node
      mode_t st_mode; //protection 文件的类型和存取的权限
      nlink_t st_nlink; //number of hard links 连到该文件的硬连接数目,
  刚建立的文件值为1.
      uid_t st_uid; //user ID of owner 文件所有者的用户识别码
      gid_t st_gid; //group ID of owner 文件所有者的组识别码
      dev_t st_rdev; //device type 若此文件为装置设备文件, 则为其设备编号
      off_t st_size; //total size, in bytes 文件大小, 以字节计算
      unsigned long st_blksize; //blocksize for filesystem I/O 文件系统的I/O
  缓冲区大小.
      unsigned long st_blocks; //number of blocks allocated 占用文件区块的个数,
  每一区块大小为512 个字节.
      time_t st_atime; //time of lastaccess 文件最近一次被存取或被执行的时间,
  一般只有在用mknod、utime、read、write 与tructate 时改变.
      time_t st_mtime; //time of last modification 文件最后一次被修改的时间,
  一般只有在用mknod、utime 和write 时才会改变
      time_t st_ctime; //time of last change i-node 最近一次被更改的时间,
  此参数会在文件所有者、组、权限被更改时更新
  };
  */
  struct stat st;
  /*becomes true if server decides this is a CGI program*/
  /*如果程序判断出是一个脚本项目则设为true*/
  int cgi = 0;
  char *query_string = NULL;

  //获取报文启始行
  numchars = get_line(client, buf, sizeof(buf));
  i = 0;
  j = 0;
  //获取请求方法
  while (!ISspace(buf[i]) && (i < sizeof(method) - 1)) {
    method[i] = buf[i];
    i++;
  }
  j = i;
  method[i] = '\0';

  /*
    函数说明：strcasecmp()用来比较参数s1和s2字符串，比较时会自动忽略大小写的差异。
    返回值：若参数s1 和s2 字符串相同则返回0。s1 长度大于s2
    长度则返回大于0的值，s1长度若小于s2 长度则返回小于0 的值。
  */
  //只支持GET、POST
  if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
    unimplemented(client);
    return;
  }

  // POST则调用CGI程序
  if (strcasecmp(method, "POST") == 0) {
    cgi = 1;
  }

  i = 0;
  //去掉多余空格
  while (ISspace(buf[j]) && (j < numchars))
    j++;
  //获取URL地址
  while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
    url[i] = buf[j];
    i++;
    j++;
  }
  url[i] = '\0';

  // GET请求，如果存在GET参数则调用CGI程序
  if (strcasecmp(method, "GET") == 0) {
    query_string = url;
    while ((*query_string != '?') && (*query_string != '\0'))
      query_string++;
    if (*query_string == '?') {
      cgi = 1;
      *query_string = '\0';
      query_string++;
    }
  }

  //定位到工作目录
  sprintf(path, "htdocs%s", url);
  if (path[strlen(path) - 1] == '/') {
    //设置默认文件
    strcat(path, "index.html");
  }
  //获取文件信息
  if (stat(path, &st) == -1) {
    //从缓存中读取并丢弃首部（释放内存？）
    while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
      numchars = get_line(client, buf, sizeof(buf));
    }
    //返回 404 NOT FOUND 响应
    not_found(client);
  } else {
    /*
      如果访问的是一个目录，则设置默认文件
      S_IFMT 0170000 文件类型的位遮罩
      S_IFDIR 0040000 目录
    */
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
      strcat(path, "/index.html");
    }
    /*
      S_IXUSR (S_IEXEC) 00100 文件所有者具可执行权限
      S_IXGRP 00010 用户组具可执行权限
      S_IXOTH 00001 其他用户具可执行权限上述的文件类型在 POSIX
      中定义了检查这些类型的宏定义
    */
    if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) ||
        (st.st_mode & S_IXOTH)) {
      cgi = 1;
    }
    if (!cgi) {
      //不是可执行文件，直接输出文件内容
      serve_file(client, path);
    } else {
      //如果是可执行文件，则执行
      execute_cgi(client, path, method, query_string);
    }
  }

  close(client);
}

/**********************************************************************/
/* 如果发起的请求是有问题的（比如POST没有数据长度属性）
 * Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "<P>Your browser sent a bad request, ");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "such as a POST without a Content-Length.\r\n");
  send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* 输出请求文件数据到SOCKET中
 * Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource) {
  char buf[1024];

  fgets(buf, sizeof(buf), resource);
  while (!feof(resource)) {
    send(client, buf, strlen(buf), 0);
    fgets(buf, sizeof(buf), resource);
  }
}

/**********************************************************************/
/* 响应web服务器错误（比如CGI执行失败）
 * Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc) {
  perror(sc);
  exit(1);
}

/**********************************************************************/
/* 执行脚本程序
 * Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method,
                 const char *query_string) {
  char buf[1024];
  int cgi_output[2];
  int cgi_input[2];
  pid_t pid;
  int status;
  int i;
  char c;
  int numchars = 1;
  int content_length = -1;

  buf[0] = 'A';
  buf[1] = '\0';
  if (strcasecmp(method, "GET") == 0) {
    //从缓存中读取并丢弃首部（释放内存？）
    while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
      numchars = get_line(client, buf, sizeof(buf));
    }
  } else if (strcasecmp(method, "POST") == 0) {
    /* POST，获取POST数据长度 */
    numchars = get_line(client, buf, sizeof(buf));
    while ((numchars > 0) && strcmp("\n", buf)) {
      buf[15] = '\0';
      if (strcasecmp(buf, "Content-Length:") == 0)
        content_length = atoi(&(buf[16]));
      numchars = get_line(client, buf, sizeof(buf));
    }
    //如果没有返回POST数据长度，响应bad_request
    if (content_length == -1) {
      bad_request(client);
      return;
    }
  } else {
    /*HEAD or other HTTP method*/
    // do something
  }

  //为什么要两个pipe，因为子进程向父进程传递回馈数据需要一个out-pipe，而若有post数据，子进程还需要一个in-pipe，从父进程读取post数据。
  //创建CGI输出管道
  if (pipe(cgi_output) < 0) {
    cannot_execute(client);
    return;
  }
  //创建CGI输入管道
  if (pipe(cgi_input) < 0) {
    cannot_execute(client);
    return;
  }

  // fork一个子线程
  if ((pid = fork()) < 0) {
    cannot_execute(client);
    return;
  }
  //发送响应状态
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);
  if (pid == 0) /* child: CGI script */
  {
    char meth_env[255];
    char query_env[255];
    char length_env[255];

    /*
      dup2()用来复制参数oldfd 所指的文件描述词, 并将它拷贝至参数newfd
      后一块返回.

      管道两端可分别用描述字fd[0]以及fd[1]来描述，需要注意的是，管道的两端是固定了任务的。
      即一端只能用于读，由描述字fd[0]表示，称其为管道读端；
      另一端则只能用于写，由描述字fd[1]来表示，称其为管道写端。
    */
    //使输出管道的写关联到标准输出
    dup2(cgi_output[1], STDOUT);
    //使输入管道的读关联到标准输入
    //如果所有管道写端对应的文件描述符被关闭，则read返回0（没有POST数据，父子线程都执行了close(cgi_input[1])）
    dup2(cgi_input[0], STDIN);
    //关闭输出管道的读
    close(cgi_output[0]);
    //关闭输入管道的写
    close(cgi_input[1]);

    sprintf(meth_env, "REQUEST_METHOD=%s", method);
    /*
      函数说明：putenv()用来改变或增加环境变量的内容.
      参数string的格式为name＝value, 如果该环境变量原先存在,
      则变量内容会依参数string 改变,否则此参数内容会成为新的环境变量.
      返回值：执行成功则返回0, 有错误发生则返回-1.

      子进程继承父进程的环境，设置环境变量可在父子间进程交流
    */
    putenv(meth_env);
    if (strcasecmp(method, "GET") == 0) {
      sprintf(query_env, "QUERY_STRING=%s", query_string);
      putenv(query_env);
    } else { /* POST */
      sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
      putenv(length_env);
    }
    /*
      函数说明：execl()用来执行参数path 字符串所代表的文件路径,
      接下来的参数代表执行该文件时传递过去的argv(0), argv[1], ...,
      最后一个参数必须用空指针(NULL)作结束.

      返回值：如果执行成功则函数不会返回, 执行失败则直接返回-1,
      失败原因存于errno 中.

      还有1个注意的是, exec函数会取代执行它的进程,
      也就是说, 一旦exec函数执行成功,它就不会返回了, 进程结束.
      但是如果exec函数执行失败, 它会返回失败的信息,而且进程继续执行后面的代码!

      执行脚本，执行后的标准输出已关联到父进程输出管道的写
    */
    execl(path, NULL);
    exit(0);
  } else { /* parent */

    //关闭输出管道的写
    close(cgi_output[1]);
    //关闭输入管道的读
    close(cgi_input[0]);
    if (strcasecmp(method, "POST") == 0) {
      /*
        POST数据写入到cgi_input[1]即写入标准输入
        作为执行perl脚本的POST数据
      */
      for (i = 0; i < content_length; i++) {
        recv(client, &c, 1, 0);
        write(cgi_input[1], &c, 1);
      }
    }
    //从输出通道（子线程中关联的标准输出）读取执行脚本的结果
    while (read(cgi_output[0], &c, 1) > 0) {
      //写入socket
      send(client, &c, 1, 0);
    }

    close(cgi_output[0]);
    close(cgi_input[1]);

    /*
      函数说明：waitpid()会暂时停止目前进程的执行, 直到有信号来到或子进程结束.
      如果在调用wait()时子进程已经结束, 则wait()会立即返回子进程结束状态值.
      子进程的结束状态值会由参数status 返回, 而子进程的进程识别码也会一快返回.
      如果不在意结束状态值, 则参数status 可以设成NULL. 参数pid
      为欲等待的子进程识别码, 其他数值意义如下：

      1、pid<-1 等待进程组识别码为pid 绝对值的任何子进程.
      2、pid=-1 等待任何子进程, 相当于wait().
      3、pid=0 等待进程组识别码与目前进程相同的任何子进程.
      4、pid>0 等待任何子进程识别码为pid 的子进程.

      参数option 可以为0 或下面的OR 组合：

      WNOHANG：如果没有任何已经结束的子进程则马上返回, 不予以等待.
      WUNTRACED：如果子进程进入暂停执行情况则马上返回, 但结束状态不予以理会.
      子进程的结束状态返回后存于status, 底下有几个宏可判别结束情况
      WIFEXITED(status)：如果子进程正常结束则为非0 值.
      WEXITSTATUS(status)：取得子进程exit()返回的结束代码, 一般会先用WIFEXITED
      来判断是否正常结束才能使用此宏.
      WIFSIGNALED(status)：如果子进程是因为信号而结束则此宏值为真
      WTERMSIG(stsatus)：取得子进程因信号而中止的信号代码, 一般会先用WIFSIGNALED
      来判断后才使用此宏.
      WIFSTOPPED(status)：如果子进程处于暂停执行情况则此宏值为真.
      一般只有使用WUNTRACED时才会有此情况.
      WSTOPSIG(status)：取得引发子进程暂停的信号代码, 一般会先用WIFSTOPPED
      来判断后才使用此宏.

      等待子进程结束，若父进程先于子进程终止，该子线程就会成为孤儿进程。
    */
    waitpid(pid, &status, 0);
  }
}

/**********************************************************************/
/* 读取请求一行内容
 * Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) {
  int i = 0;
  char c = '\0';
  int n;

  while ((i < size - 1) && (c != '\n')) {
    n = recv(sock, &c, 1, 0);
    /* DEBUG printf("%02X\n", c); */
    if (n > 0) {
      //处理不同系统中换行符差异
      if (c == '\r') {
        n = recv(sock, &c, 1, MSG_PEEK);
        /* DEBUG printf("%02X\n", c); */
        if ((n > 0) && (c == '\n'))
          recv(sock, &c, 1, 0);
        else
          c = '\n';
      }
      buf[i] = c;
      i++;
    } else
      c = '\n';
  }
  buf[i] = '\0';

  return (i);
}

/**********************************************************************/
/* 设置首部信息
 * Return the informational HTTP headers about a file.
 * Parameters: the socket to print the headers on
 *             the name of the file
 */
/**********************************************************************/
void headers(int client, const char *filename) {
  char buf[1024];
  /* 可以根据文件名来决定返回的文件类型 */
  (void)filename;

  strcpy(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "your request because the resource specified\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "is unavailable or nonexistent.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 不是可执行文件，直接输出文件内容
 * Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename) {
  FILE *resource = NULL;
  int numchars = 1;
  char buf[1024];

  buf[0] = 'A';
  buf[1] = '\0';
  //从缓存中读取并丢弃首部（释放内存？）
  while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
    numchars = get_line(client, buf, sizeof(buf));
  }

  //读取请求文件
  resource = fopen(filename, "r");
  if (resource == NULL) {
    //空资源返回 404 NOT FOUND
    not_found(client);
  } else {
    //设置首部
    headers(client, filename);
    //输出请求文件数据到SOCKET中
    cat(client, resource);
  }
  fclose(resource);
}

/**********************************************************************/
/* 开启web服务器，监听端口的请求
 * This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port) {
  int httpd = 0;
  int on = 1;
  struct sockaddr_in name;

  httpd = socket(PF_INET, SOCK_STREAM, 0);
  if (httpd == -1)
    error_die("socket");
  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(*port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
    error_die("setsockopt failed");
  }
  if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    error_die("bind");
  if (*port == 0) /* if dynamically allocating a port */
  {
    socklen_t namelen = sizeof(name);
    if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
      error_die("getsockname");
    *port = ntohs(name.sin_port);
  }
  if (listen(httpd, 5) < 0)
    error_die("listen");
  return (httpd);
}

/**********************************************************************/
/* HTTP方法不支持提示
 * Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</TITLE></HEAD>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void) {
  int server_sock = -1;
  u_short port = 4000;
  int client_sock = -1;
  struct sockaddr_in client_name;
  socklen_t client_name_len = sizeof(client_name);
  //   pthread_t newthread;

  server_sock = startup(&port);
  printf("httpd running on port %d\n", port);

  while (1) {
    client_sock =
        accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
    if (client_sock == -1)
      error_die("accept");
    accept_request(client_sock);
    // if (pthread_create(&newthread, NULL, (void *)accept_request,
    //                    (void *)&client_sock) != 0)
    //   perror("pthread_create");
  }

  close(server_sock);

  return (0);
}
