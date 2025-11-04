/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

/**
*	@brief 한 개의 HTTP 트랜잭션을 처리
*	@details 
*	@param fd 클라이언트 소켓 파일 디스크립터
*	@return void(반환값 없음)
*	@throw 발생 예외 없음
*
*/
void doit(int fd);

/**
* @brief 전체 헤더들을 빈줄을 만날때까지 읽어들이기
* @details
* @param rp RIO(버퍼 읽어들이는 구조체) 포인터
* @return void(반환값 없음)
* @throw 발생 예외 없음
* 
*/
void read_requesthdrs(rio_t *rp);

/**
*	@brief URI 구조화하여 분리, 정적/동적 콘텐츠 분류
*	@details 
*	@param uri URI
* @param filename 파일 이름이 저장될 곳
* @param cgiargs ?뒤에 있는 CGI 인자 값들을 저장
*	@return int(반환값 있음)
*	@throw 발생 예외 없음
*
*/
int parse_uri(char *uri, char *filename, char *cgiargs);

/**
*	@brief 정적 파일을 클라이언트에 전송 
*	@details 
*	@param fd 클라이언트 소켓 파일 디스크럽터
* @param filename 전송할 파일 경로
* @param filesize 파일 크기
*	@return void(반환값 없음)
*	@throw 발생 예외 없음
*
*/
void serve_static(int fd, char *filename, int filesize);

/**
*	@brief 파일 확장자를 보고 MIME 타입을 결정한다
*	@details 
*	@param filename 파일 이름
* @param filetype MIME 타입을 저장할 곳
*	@return void(반환값 없음)
*	@throw 발생 예외 없음
*
*/
void get_filetype(char *filename, char *filetype);

/**
*	@brief 동적 파일을 클라이언트에 전송
*	@details 
*	@param fd 클라이언트 소켓 파일 디스크럽터
* @param filename 실행할 CGI 프로그램 경로
* @param cgiargs ?뒤에 있는 CGI 인자 값들을 저장
*	@return void(반환값 없음)
*	@throw 발생 예외 없음
*
*/
void serve_dynamic(int fd, char *filename, char *cgiargs);

/**
 * @brief 클라이언트에게 HTTP 에러 응답을 전송한다
 * @details 
 * @param fd 클라이언트 소켓 파일 디스크럽터
 * @param cause 에러 원인
 * @param errnum 에러 코드
 * @param shortmsg 짧은 메시지
 * @param longmsg 상세 메시지
 * @return void(반환값 없음)
 * @throw 발생 예외 없음
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
/////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 *
 *	@brief 서버 구동, 유효한 서버 인자 값을 받고 서버 유지를 위해 무한 반복 한다.
 *	@details 
 *	@param argc 운영체제에서 입력 받는 인자 개수
 *  @param argv [0]는 프로그램 이름, [1]은 포트번호
 *	@return int(반환값 있음)
 *	@throw 발생 예외 없음
 *
 */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) // 서버 인자 크기는 프로그램 이름 + 포트번호로 무조건 2여야 한다.
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // 문제점 알아내기 위하여 출력
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 포트에서 연결 요청을 기다리기 위한 listening socket 생성
  while (1) // 서버는 상시 가동상태여야 함
  {
    clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체 크기 설정 (Accept 함수에 필요)
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 수락하고 통신용 소켓 생성
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트 IP와 포트를 문자열로 변환 (로깅용)
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // 클라이언트 HTTP 요청 처리
    Close(connfd); // 연결 소켓 닫기
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /*Read request line and headers*/
  /* 클라이언트의 HTTP 요청을 수신하여, 요청 메서드와 헤더 정보를 파싱한다 */
  Rio_readinitb(&rio, fd); // Rio 버퍼 초기화                                   
  Rio_readlineb(&rio, buf, MAXLINE); // 버퍼 값 읽기
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s%s%s", method, uri, version);
  if (strcasecmp(method, "GET")) //메서드가 GET이 여야한다.
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); //버퍼 전체 읽기

  /*Parse URI from GET request */
  /*GET 요청으로부터 URI를 얻기 */
  is_static = parse_uri(uri, filename, cgiargs); // 동적, 정적 서버 구분
  if (stat(filename, &sbuf) < 0) //요청된 파일이 유효한지 확인
  {
    clienterror(fd, filename, "404", "Notfound", "Tiny couldn’t find this file");
    return;
  }

  if (is_static)
  { /*Serve static content*/
    /*정적 콘텐츠*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) //파일이 일반 파일인지, 권한이 있는지 확인
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn’t read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  else 
  { /*Serve dynamic content*/
    /*동적 콘텐츠*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) //파일이 일반 파일인지, 권한이 있는지 확인
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny  couldn’t run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body
    // HTTP 응답 본문 생성 시작(에러메시지)
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body + strlen(body), "<body bgcolor=\"ffffff\">\r\n");
    sprintf(body + strlen(body), "%s: %s\r\n", errnum, shortmsg);
    sprintf(body + strlen(body), "<p>%s: %s\r\n", longmsg, cause);
    sprintf(body + strlen(body), "<hr><em>The Tiny Web server</em>\r\n");

    // Print the HTTP response
    // HTTP 응답 전송
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %lu\r\n\r\n", strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while (strcmp(buf, "\r\n")) //헤더들 전체를 읽기 위해
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) // cgi-bin 문자열이 포함되어 있지 않은 경우 정적 콘텐츠
  { /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/') // URI가 '/'로 끝난다면 (예: http://host/) 디렉토리 요청으로 간주
      strcat(filename, "home.html");
    return 1; //정적 콘텐츠임을 알림
  }
  else
  { /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0; //동적 콘텐츠임을 알림
  }
}

void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // Send response headers to client (클라이언트에게 응답 헤더 전송)
    get_filetype(filename, filetype);  // Determine file type (파일 타입 결정) (e.g., text/html)
    sprintf(buf, "HTTP/1.0 200 OK\r\n");

    sprintf(buf + strlen(buf), "Server: Tiny Web Server\r\n");
    sprintf(buf + strlen(buf), "Content-length: %d\r\n", filesize);
    sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", filetype);
    
    Rio_writen(fd, buf, strlen(buf));  // Send headers (헤더 전송) (클라이언트 소켓에 버퍼 내용 쓰기)

    // Send response body to client (클라이언트에게 응답 본문 전송)
    srcfd = Open(filename, O_RDONLY, 0);  // Open the file (파일 열기) (읽기 전용으로 요청 파일 열고 디스크립터 얻기)
    
    // Memory-map the file (파일을 메모리 맵핑) (파일 내용을 메모리 주소 공간에 매핑)
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); 
    
    Close(srcfd);  // File descriptor no longer needed (파일 디스크립터 닫기) (메모리 맵핑 후 원본 파일 디스크립터 닫기)
    
    Rio_writen(fd, srcp, filesize);  // Send file content (파일 내용 전송) (메모리에 맵핑된 파일 내용을 클라이언트 소켓에 쓰기)
    
    Munmap(srcp, filesize);  // Unmap the file (파일 매핑 해제) (메모리 맵핑 해제 및 자원 반환)
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXLINE], *emptylist[] = { NULL };

    // Return first part of HTTP response
    // HTTP의 응답 헤더 전송
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0) {  // Child process(자식 프로세스)
        // Real server would set all CGI vars here (실제 서버라면 모든 CGI 변수를 설정해야 함)
        setenv("QUERY_STRING", cgiargs, 1);
        dup2(fd, STDOUT_FILENO);  // Redirect stdout to client (표준 출력(1)을 클라이언트 소켓(fd)으로 복제하여 리디렉션)
        Execve(filename, emptylist, environ);  // Run CGI program (CGI 프로그램 실행)
    }
    Wait(NULL);  // Parent waits for child to terminate (부모는 자식이 종료될 때까지 블로킹하며 대기)
}

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

