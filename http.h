#include <sys/types.h>
#include <assert.h>

extern char  ip[128];

typedef enum
{
    HTTP_GET,
    HTTP_PUT,
    HTTP_POST,
    HTTP_OPTIONS,
    HTTP_HEAD,
    HTTP_DELETE,
    HTTP_TRACE
} http_met_t;

typedef struct http_header http_hdr_t;

struct http_header {
    const  char           *name;
    const  char           *value;
    http_hdr_t            *next;
};

typedef struct {
    http_met_t             method;
    const char            *uri;
    int                    major_version;
    int                    minor_version;
    http_hdr_t            *header;
    size_t                 content_len;
    char                  *content;
} http_req_t;

typedef struct {
    int                   major_version;
    int                   minor_version;
    int                   status_code;
    const char           *status_message;
    http_hdr_t           *header;
    size_t                content_len;
    unsigned char        *content;
} http_res_t;

typedef struct {
    const char            *uri;
    const char            *host_name;
    unsigned short         host_port;
    size_t                 content_len;
    char                  *content;
} http_des_t;

ssize_t http_get (http_des_t *dest);
ssize_t http_post (http_des_t *dest); 
ssize_t http_put (http_des_t *dest);
ssize_t http_options (http_des_t *dest);
ssize_t http_head (http_des_t *dest);
ssize_t http_delete (http_des_t *dest);
ssize_t http_trace (http_des_t *dest);

unsigned short validate_port (unsigned short port);
int connect_to_server (const char *host, unsigned short port);
int get_ip_from_host (char *ipbuf, const char *host, int maxlen);


const char *http_header_get (http_hdr_t *header, const char *name);

http_res_t *http_create_response (int major_version,
    int minor_version, int status_code, const char *status_message);
ssize_t http_parse_response (int fd, http_res_t **response);
int http_error_to_errno (int err);
void http_destroy_response (http_res_t *response);


http_req_t *http_create_request (http_met_t method,
    const char *uri, int major_version, int minor_version);
http_hdr_t *http_add_header (http_hdr_t **header,
	const char *name, const char *value);
ssize_t http_write_request (int fd, http_req_t *request);
void http_destroy_request (http_req_t *resquest);
ssize_t http_parse_request (int fd, http_req_t **request);

