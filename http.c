#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>

#include "http.h"

static void __die__ (const char* ret);
static ssize_t __read_all__ (int fd, void *buf, size_t len);
static ssize_t __write_all__ (int fd, void *data, size_t len);
static ssize_t __read_until__ (int fd, int ch, unsigned char **data);

static http_hdr_t *__http_header_find__ (http_hdr_t *header,
    const char *name);
static ssize_t __http_method__ (int fd, http_des_t *dest,
    http_met_t method);

static http_met_t __http_string_to_method__ (const char *method, size_t n);
static const char *__http_method_to_string__ (http_met_t method);

static ssize_t __parse_header__ (int fd, http_hdr_t **header);

void __dynamic_read_socket__ (int fd, http_res_t *response);

static http_req_t *__http_allocate_request__ (const char *uri);
static http_res_t *__http_allocate_response__ (const char *status_message);
static http_hdr_t *__http_alloc_header__ (const char *name,
    const char *value);

static void __http_destroy_header__ (http_hdr_t *header);
static ssize_t __http_write_header__ (int fd, http_hdr_t *header);
static http_hdr_t *__http_header_find__ (http_hdr_t *header,
    const char *name);


static ssize_t
__read_all__ (int fd, void *vptr, size_t n)
{
    char    *ptr;
    size_t   nleft;
    ssize_t  nread;

    ptr = vptr;
    nleft = n;

    while (nleft > 0) {
        if ( (nread = read (fd, ptr, nleft)) < 0) {
            if (errno == EINTR) {
                printf("nonblocking\n");
                nread = 0; /*and call read() again*/
            }
            else
                return(-1);
        } else if (nread == 0)
            break;          /*EOF*/
        nleft -= nread;
        ptr   += nread;
    }
    return n - nleft;
}

static ssize_t
__write_all__ (int fd, void *vptr, size_t n)
{
    size_t         nleft;
    ssize_t        nwritten;
    const char*    ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nwritten = write (fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0; /*and call write() again*/
            else
                return -1;
        }

        nleft -= nwritten;
        ptr += nwritten;
    }
    return n;
}

static ssize_t
__http_method__ (int fd, http_des_t *dest, http_met_t method)
{
    ssize_t        n;
    char           str[1024];
    http_req_t    *request;

    request = http_create_request (method, dest->uri, 1, 1);
    if (request == NULL)
        return -1;

    sprintf (str, "%s:%d", dest->host_name, dest->host_port);
    http_add_header (&request->header, "Host", str);
    http_add_header (&request->header,
                     "User-Agent",
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 \
                     (KHTML, like Gecko) Chrome/46.0.2490.80 Safari/537.36");
    http_add_header (&request->header, "Connection", "close");
    if (dest->content_len > 0 && dest->content != NULL) {
        sprintf (str, "%ld", dest->content_len);
        http_add_header (&request->header,
                         "Content-Type",
                         "application/x-www-form-urlencoded");
        http_add_header (&request->header, "Content-Length", str);

        request->content_len = dest->content_len;
        request->content = dest->content;
    }

    n = http_write_request (fd, request);
    http_destroy_request (request);

    return n;
}

ssize_t
http_get (http_des_t *dest)
{
    int  fd;

    validate_port (dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        /*http get don't have content,so init it NULL'*/
        dest->content_len = 0;
        dest->content = NULL;

        if (__http_method__ (fd, dest, HTTP_GET) > 0)
            return fd;
        else
            return -1;
    } else
        return fd;
}

ssize_t
http_put (http_des_t *dest)
{
    int  fd;

    validate_port (dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        if (__http_method__ (fd, dest, HTTP_PUT) > 0)
            return fd;
        else
            return -1;
    } else
        return 1;
}

ssize_t
http_post (http_des_t *dest)
{
    int  fd;

    validate_port (dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        if (__http_method__ (fd, dest, HTTP_POST) > 0)
            return fd;
        else
            return -1;
    } else
        return -1;
}

ssize_t
http_trace (http_des_t *dest)
{
    int  fd;

    validate_port(dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        if (__http_method__ (fd, dest, HTTP_TRACE) > 0)
            return fd;
        else
            return -1;
    } else
        return -1;
}

ssize_t
http_delete (http_des_t *dest)
{
    int  fd;

    validate_port(dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        /*http delete method don't have body,so init it NULL'*/
        dest->content_len = 0;
        dest->content = NULL;

        if (__http_method__ (fd, dest, HTTP_DELETE) > 0)
            return fd;
        else
            return -1;
    } else
        return -1;
}

ssize_t
http_options (http_des_t *dest)
{
    int  fd;

    validate_port(dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        /*http options method don't have body,so init it NULL'*/
        dest->content_len = 0;
        dest->content = NULL;

        if (__http_method__ (fd, dest, HTTP_OPTIONS) > 0)
            return fd;
        else
            return -1;
    } else
        return -1;
}

ssize_t
http_head (http_des_t *dest)
{
    int  fd;

    validate_port(dest->host_port);

    fd = connect_to_server (dest->host_name, dest->host_port);
    if (fd > 0) {
        /*http head method don't have body,so init it NULL'*/
        dest->content_len = 0;
        dest->content = NULL;

        if (__http_method__ (fd, dest, HTTP_HEAD) > 0)
            return fd;
        else
            return -1;
    } else
        return -1;
}

static void
__die__ (const char* ret)
{
    fprintf(stderr, "%s\n", ret);
    exit(-1);
}

int
get_ip_from_host (char *ipbuf, const char *host, int maxlen)
{
    struct sockaddr_in  sa;
    struct hostent     *he;

    sa.sin_family = AF_INET;
    if (inet_aton (host, &sa.sin_addr) == 0) {
        he = gethostbyname (host);
        if (he == NULL)
            __die__("gethostbyname error.");
        memcpy (&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    strncpy (ipbuf, inet_ntoa(sa.sin_addr), maxlen);

    return 0;
}

int
connect_to_server (const char *host, unsigned short port) {
    int                 sockfd;
    struct sockaddr_in  address;
    int                 conn_ret;
    char                ipBuf[128] = {0};
    int                 i = 2;

    do {
        if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) > 0)
            break;
    } while (i--);
    i = 2;

    address.sin_family = AF_INET;
    if (ip[0] != '\0') {
        address.sin_addr.s_addr = inet_addr(ip);
    } else {
        if (get_ip_from_host (ipBuf, host, 128) != 0) {
             __die__("unknown error.");
        }
        address.sin_addr.s_addr = inet_addr(ipBuf);
    }
    address.sin_port = htons(port);

    do {
        conn_ret = connect (sockfd, (struct sockaddr *)&address, sizeof(address));
        if (conn_ret == 0) return sockfd;
    } while (i--);

    return -2;
}

int
http_error_to_errno (int err)
{
    /* Error codes taken from RFC2068. */
    switch (err) {

    case -1: /* system error */
        return errno;
    case -200: /* OK */
    case -201: /* Created */
    case -202: /* Accepted */
    case -203: /* Non-Authoritative Information */
    case -204: /* No Content */
    case -205: /* Reset Content */
    case -206: /* Partial Content */
        return 0;
    case -400: /* Bad Request */
        return EIO;
    case -401: /* Unauthorized */
        return EACCES;
    case -403: /* Forbidden */
        return EACCES;
    case -404: /* Not Found */
        return ENOENT;
    case -411: /* Length Required */
        return EIO;
    case -413: /* Request Entity Too Large */
        return EIO;
    case -505: /* HTTP Version Not Supported       */
        return EIO;
    case -100: /* Continue */
    case -101: /* Switching Protocols */
    case -300: /* Multiple Choices */
    case -301: /* Moved Permanently */
    case -302: /* Moved Temporarily */
    case -303: /* See Other */
    case -304: /* Not Modified */
    case -305: /* Use Proxy */
    case -402: /* Payment Required */
    case -405: /* Method Not Allowed */
    case -406: /* Not Acceptable */
    case -407: /* Proxy Autentication Required */
    case -408: /* Request Timeout */
    case -409: /* Conflict */
    case -410: /* Gone */
    case -412: /* Precondition Failed */
    case -414: /* Request-URI Too Long */
    case -415: /* Unsupported Media Type */
    case -500: /* Internal Server Error */
    case -501: /* Not Implemented */
    case -502: /* Bad Gateway */
    case -503: /* Service Unavailable */
    case -504: /* Gateway Timeout */
        return EIO;
    default:
        return EIO;
    }
}

static http_met_t
__http_string_to_method__ (const char *method, size_t n)
{
    if (strncmp (method, "GET", n) == 0)
        return HTTP_GET;
    if (strncmp (method, "PUT", n) == 0)
        return HTTP_PUT;
    if (strncmp (method, "POST", n) == 0)
        return HTTP_POST;
    if (strncmp (method, "OPTIONS", n) == 0)
        return HTTP_OPTIONS;
    if (strncmp (method, "HEAD", n) == 0)
        return HTTP_HEAD;
    if (strncmp (method, "DELETE", n) == 0)
        return HTTP_DELETE;
    if (strncmp (method, "TRACE", n) == 0)
        return HTTP_TRACE;
    return -1;
}

static const char *
__http_method_to_string__ (http_met_t method)
{
    switch (method) {

    case HTTP_GET:
        return "GET";
    case HTTP_PUT:
        return "PUT";
    case HTTP_POST:
        return "POST";
    case HTTP_OPTIONS:
        return "OPTIONS";
    case HTTP_HEAD:
        return "HEAD";
    case HTTP_DELETE:
        return "DELETE";
    case HTTP_TRACE:
        return "TRACE";
    default:
        return "(UNKNOW)";
    }
}

static ssize_t
__read_until__ (int fd, int ch, unsigned char **data)
{
    unsigned char *buf, *buf2;
    ssize_t        n, len, buf_size;

    *data = NULL;

    buf_size = 100;
    buf = malloc (buf_size);
    if (buf == NULL) {
        return -1;
    }

    len = 0;
    while ((n = __read_all__ (fd, buf + len, 1)) == 1) {
        if (buf[len++] == ch)
            break;
        if (len + 1 == buf_size) {
            buf_size *= 2;
            buf2 = realloc (buf, buf_size);
            if (buf2 == NULL) {
                free (buf);
                return -1;
            }
            buf = buf2;
        }
    }
    if (n <= 0) {
        free (buf);
        if (n == 0)
            printf ("read_until: closed\n");
        else
            printf ("read_until: read error: %s\n", strerror (errno));
        return n;
    }

    /* Shrink to minimum size + 1 in case someone wants to add a NUL. */
    buf2 = realloc (buf, len + 1);
    if (buf2 == NULL)
        printf ("read_until: realloc: shrink failed\n"); /* not fatal */
    else
        buf = buf2;

    *data = buf;
    return len;
}

static http_hdr_t *
__http_alloc_header__ (const char *name, const char *value) {
    http_hdr_t  *header;

    header = malloc (sizeof (http_hdr_t));
    if (header == NULL)
        goto fail3;

    header->name = strdup (name);
    if (header->name == NULL)
        goto fail2;
    header->value = strdup (value);
    if (header->value == NULL) {
        goto fail1;
    }

    return header;

fail1:
    free ((char *)header->name);
fail2:
    free (header);
fail3:
    return NULL;
}

http_hdr_t *
http_add_header (http_hdr_t **header, const char *name,
    const char *value)
{
    http_hdr_t  *new_header;

    new_header = __http_alloc_header__ (name, value);
    if (new_header == NULL)
        return NULL;

    new_header->next = NULL;
    while (*header)
        header = &(*header)->next;
    *header = new_header;

    return new_header;
}

static ssize_t
__parse_header__ (int fd, http_hdr_t **header)
{
    ssize_t        n;
    http_hdr_t    *h;
    size_t         len;
    unsigned char *data;
    unsigned char  buf[2];

    *header = NULL;

    n = __read_all__ (fd, buf, 2);
    if (n <= 0)
        return n;
    if (buf[0] == '\r' && buf[1] == '\n')
        return n;

    h = malloc (sizeof (http_hdr_t));
    if (h == NULL)
    {
        printf ("parse_header: malloc failed\n");
        return -1;
    }
    *header = h;
    h->name = NULL;
    h->value = NULL;

    n = __read_until__ (fd, ':', &data);
    if (n <= 0)
        return n;
    data = realloc (data, n + 2);
    if (data == NULL)
    {
        printf ("parse_header: realloc failed\n");
        return -1;
    }
    memmove (data + 2, data, n);
    memcpy (data, buf, 2);
    n += 2;
    data[n - 1] = 0;
    h->name = data;
    len = n;

    n = __read_until__ (fd, '\r', &data);
    if (n <= 0)
        return n;
    data[n - 1] = 0;
    h->value = data;
    len += n;

    n = __read_until__ (fd, '\n', &data);
    if (n <= 0)
        return n;
    free (data);
    if (n != 1) {
        printf ("parse_header: invalid line ending\n");
        return -1;
    }
    len += n;


    n = __parse_header__ (fd, &h->next);
    if (n <= 0)
        return n;
    len += n;

    return len;
}

static ssize_t
__http_write_header__ (int fd, http_hdr_t *header)
{
    ssize_t  n = 0, m;

    if (header == NULL)
        return __write_all__ (fd, "\r\n", 2);

    m = __write_all__ (fd, (void *)header->name, strlen (header->name));
    if (m == -1)
        return -1;
    n += m;

    m = __write_all__ (fd, ": ", 2);
    if (m == -1)
        return -1;
    n += m;

    m = __write_all__ (fd, (void *)header->value, strlen (header->value));
    if (m == -1)
        return -1;
    n += m;

    m = __write_all__ (fd, "\r\n", 2);
    if (m == -1)
        return -1;
    n += m;

    m = __http_write_header__ (fd, header->next);
    if (m == -1)
        return -1;
    n += m;

    return n;
}

unsigned short
validate_port(unsigned short port) {
    if (port <= 0 || port > 0xffff) {
            printf("Invalid port %d\n", port);
            exit(-1);
        }
    /* return port number if its valid */
    return port;
}

static void
__http_destroy_header__ (http_hdr_t *header)
{
    if (header == NULL)
        return;

    __http_destroy_header__ (header->next);

    if (header->name)
        free ((char *)header->name);
    if (header->value)
        free ((char *)header->value);
    free (header);
}

static http_res_t *
__http_allocate_response__ (const char *status_message)
{
    http_res_t  *response;

    response = malloc (sizeof (http_res_t));
    if (response == NULL)
        return NULL;

    response->status_message = strdup(status_message);
    if (response->status_message == NULL) {
        free (response);
        return NULL;
    }

    return response;
}

http_res_t *
http_create_response (int major_version,
    int minor_version, int status_code, const char *status_message)
{
    http_res_t  *response;

    response = __http_allocate_response__ (status_message);
    if (response == NULL)
        return NULL;

    response->major_version = major_version;
    response->minor_version = minor_version;
    response->status_code = status_code;
    response->header = NULL;

    return response;
}

ssize_t
http_parse_response (int fd, http_res_t **response_)
{
    ssize_t         n;
    unsigned char   ch;
    size_t          len;
    unsigned char  *data;
    http_res_t     *response;
    int             readn;
    unsigned char  *content;
    int             malloc_size = 1024;
    int             effiv_addr  = 0;

    *response_ = NULL;

    response = malloc (sizeof (http_res_t));
    if (response == NULL) {
        printf ("http_parse_response: out of memory\n");
        return -1;
    }

    response->major_version = -1;
    response->minor_version = -1;
    response->status_code = -1;
    response->status_message = NULL;
    response->header = NULL;

    n = __read_until__ (fd, '/', &data);
    if (n <= 0) {
        free (response);
        return n;
    } else if (n != 5 || memcmp (data, "HTTP", 4) != 0) {
        printf ("http_parse_response: expected \"HTTP\"\n");
        free (data);
        free (response);
        return -1;
    }
    free (data);
    len = n;

    n = __read_until__ (fd, '.', &data);
    if (n <= 0) {
        free (response);
        return n;
    }
    data[n - 1] = 0;
    response->major_version = atoi (data);
    free (data);
    len += n;

    n = __read_until__ (fd, ' ', &data);
    if (n <= 0) {
        free (response);
        return n;
    }
    data[n - 1] = 0;
    response->minor_version = atoi (data);
    free (data);
    len += n;

    n = __read_until__ (fd, ' ', &data);
    if (n <= 0) {
        free (response);
        return n;
    }
    data[n - 1] = 0;
    response->status_code = atoi (data);
    free (data);
    len += n;

    n = __read_until__ (fd, '\r', &data);
    if (n <= 0) {
        free (response);
        return n;
    }
    data[n - 1] = 0;
    response->status_message = data;
    len += n;

    n = __read_until__ (fd, '\n', &data);
    if (n <= 0) {
        http_destroy_response (response);
        return n;
    }
    free (data);
    if (n != 1) {
        printf ("http_parse_response: invalid line ending\n");
        http_destroy_response (response);
        return -1;
    }
    len += n;

    n =__parse_header__ (fd, &response->header);
    if (n <= 0) {
        http_destroy_response (response);
        return n;
    }
    len += n;

    response->content_len = 0;
    
    content = (unsigned char *) malloc(malloc_size);
    /*parse chunked data*/
    if (http_header_get (response->header, "Transfer-Encoding")) {
        while ((n = __read_until__ (fd, '\n', &data)) > 0) {
            data[n - 2] = '\0';

            if ((readn = (int) strtol (data, NULL, 16)) > 0) {
                malloc_size += readn;
                content = (unsigned char *) realloc ((void*)content, malloc_size);
                __read_all__(fd, content + effiv_addr, readn);
                effiv_addr += readn;

                /*unused CLRF*/
                read (fd, &ch, 1);
                assert (ch == '\r');
                read (fd, &ch, 1);
                assert (ch == '\n');
            } else {
                /* chunk size is 0 */
                /*unused CLRF*/
                read (fd, &ch, 1);
                assert (ch == '\r');
                read (fd, &ch, 1);
                assert (ch == '\n');
                break;
            }
        }
        content[effiv_addr] = '\0';
        response->content_len = effiv_addr;
        response->content = content;
    } else {
        __dynamic_read_socket__ (fd, response);
    }

    *response_ = response;
    return len;
}

void 
__dynamic_read_socket__ (int fd, http_res_t* response)
{
    int             readn;
    unsigned char  *body;
    int             malloc_size = 1024;
    int             block_size  = 1024;
    int             effiv_addr  = 0;

    body = (unsigned char*) malloc (malloc_size);
    while ((readn = __read_all__ (fd, body + effiv_addr, block_size)) > 0) {
        effiv_addr += readn;
        malloc_size += block_size;
        body = (unsigned char*) realloc ((void*)body, malloc_size);
    }
    
    response->content = body;
    response->content_len = effiv_addr;
}

void
http_destroy_response (http_res_t *response)
{
    if (response->status_message)
        free ((char *)response->status_message);
    __http_destroy_header__ (response->header);
    free(response->content);
    free (response);
}

static http_req_t *
__http_allocate_request__ (const char *uri)
{
    http_req_t  *request;

    request = malloc (sizeof (http_req_t));
    if (request == NULL)
        return NULL;

    request->uri = strdup (uri);
    if (request->uri == NULL) {
        free (request);
        return NULL;
    }

    return request;
}

http_req_t *
http_create_request (http_met_t method,
    const char *uri, int major_version, int minor_version)
{
    http_req_t  *request;

    request =  __http_allocate_request__ (uri);
    if (request == NULL)
        return NULL;

    request->method = method;
    request->major_version = major_version;
    request->minor_version = minor_version;
    request->header = NULL;

    return request;
}

ssize_t
http_parse_request (int fd, http_req_t **request_)
{
    ssize_t        n;
    size_t         len;
    unsigned char *data;
    http_req_t    *request;

    *request_ = NULL;

    request = malloc (sizeof (http_req_t));
    if (request == NULL) {
        return -1;
    }

    request->method = -1;
    request->uri = NULL;
    request->major_version = -1;
    request->minor_version = -1;
    request->header = NULL;

    n = __read_until__ (fd, ' ', &data);
    if (n <= 0) {
        free (request);
        return n;
    }
    request->method = __http_string_to_method__ (data, n - 1);
    if (request->method == -1) {
        free (data);
        free (request);
        return -1;
    }
    data[n - 1] = 0;
    free (data);
    len = n;

    n = __read_until__ (fd, ' ', &data);
    if (n <= 0) {
        free (request);
        return n;
    }
    data[n - 1] = 0;
    request->uri = data;
    len += n;

    n = __read_until__ (fd, '/', &data);
    if (n <= 0) {
        http_destroy_request (request);
        return n;
    } else if (n != 5 || memcmp (data, "HTTP", 4) != 0) {
        free (data);
        http_destroy_request (request);
        return -1;
    }
    free (data);
    len = n;

    n = __read_until__ (fd, '.', &data);
    if (n <= 0) {
        http_destroy_request (request);
        return n;
    }
    data[n - 1] = 0;
    request->major_version = atoi (data);
    free (data);
    len += n;

    n = __read_until__ (fd, '\r', &data);
    if (n <= 0) {
        http_destroy_request (request);
        return n;
    }
    data[n - 1] = 0;
    request->minor_version = atoi (data);
    free (data);
    len += n;
    n = __read_until__ (fd, '\n', &data);
    if (n <= 0) {
        http_destroy_request (request);
        return n;
    }
    free (data);
    if (n != 1) {
        http_destroy_request (request);
        return -1;
    }
    len += n;

    n = __parse_header__ (fd, &request->header);
    if (n <= 0) {
        http_destroy_request (request);
        return n;
    }
    len += n;

    *request_ = request;
    return len;
}

ssize_t
http_write_request (int fd, http_req_t *request)
{
    ssize_t  m;
    ssize_t  n = 0;
    char     str[1024];

    m = snprintf (str, 1024, "%s %s HTTP/%d.%d\r\n",
                  __http_method_to_string__ (request->method),
                  request->uri,
                  request->major_version,
                  request->minor_version);
    m = __write_all__ (fd, str, m);
    /*printf ("http_write_request: %s", str);*/
    if (m == -1) {
        printf ("http_write_request: write error: %s\n", strerror (errno));
        return -1;
    }
    n += m;

    m = __http_write_header__ (fd, request->header);
    if (m == -1)
        return -1;
    n += m;

    if (request->content != NULL && request->content_len > 0) {
        m = __write_all__(fd, request->content, request->content_len);
        if (m == -1)
            return -1;
        n += m;
    }

    return n;
}

void
http_destroy_request (http_req_t *request)
{
    if (request->uri)
        free ((char *)request->uri);
    __http_destroy_header__ (request->header);
    free (request);
}

static http_hdr_t *
__http_header_find__ (http_hdr_t *header, const char *name)
{
    if (header == NULL)
        return NULL;

    if (strcmp (header->name, name) == 0)
        return header;

    return __http_header_find__ (header->next, name);
}

const char *
http_header_get (http_hdr_t *header, const char *name)
{
    http_hdr_t *h;

    h = __http_header_find__ (header, name);
    if (h == NULL)
        return NULL;

    return h->value;
}
