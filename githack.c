#include <stddef.h>
#include "githack.h"
#include <curl/curl.h>

static char            *url = NULL;
static struct           url_combo url_combo;
static unsigned short   port = DEFAULT_PORT;
char                    ip[128] = {0};

int
hex2dec (unsigned char *hex, int len)
{
    int   i;
    char  buf[3], format[BUFFER_SIZE] = {'\0'};

    strcat (format, "0x");
    for (i = 0; i < len; i++) {
        sprintf (buf, "%02x", hex[i]);
        strcat (format, buf);
    }
    return (int) strtol (format, NULL, 16);
}

char *
sha12hex (unsigned char *sha1)
{
    int     i;
    char    buf[3], result[SHA1_SIZE / 4 + 1];

    for (i = 0; i < SHA1_SIZE / 8; i++) {
        sprintf (buf, "%02x", sha1[i]);
        strcat (result, buf);
    }

    return strdup (result);
}

bool
signature_check (magic_hdr * magic_head)
{
    return magic_head->signature[0] == 'D'
        && magic_head->signature[1] == 'I'
        && magic_head->signature[2] == 'R'
        && magic_head->signature[3] == 'C' ? true : false;
}

bool
version_check (magic_hdr * magic_head)
{
    int     version;

    version = hex2dec (magic_head->version, 4);
    return version == 2 || version == 3 || version == 4 ? true : false;
}

void
init_check (int sockfd, magic_hdr * magic_head)
{
    read (sockfd, magic_head, sizeof (magic_hdr));
    assert (signature_check (magic_head) == true);
    assert (version_check (magic_head) == true);
}

int
sed2bed (int value)
{
    return ((value & 0x000000FF) << 24) |
        ((value & 0x0000FF00) << 8) |
        ((value & 0x00FF0000) >> 8) | ((value & 0xFF000000) >> 24);
}

void
pad_entry (int sockfd, int entry_len)
{
    char pad;
    int  i, padlen;

    padlen = (8 - (entry_len % 8)) ? (8 - (entry_len % 8)) : 8;
    for (i = 0; i < padlen; i++)
    {
        readn (sockfd, &pad, 1);
        assert (pad == '\0');
    }

}

char *
get_name (int sockfd, size_t namelen, int *entry_len)
{
    char    *name;

    name = (char *) calloc ((unsigned short)0x0FFF, 1);
    if (namelen < (unsigned short)0x0FFF) {
        readn (sockfd, name, namelen);
    } else {
        /*read name error, skip*/
    }
    *entry_len += namelen;
    return name;
}

void
handle_version3orlater (int sockfd, int *entry_len)
{
    struct _extra_flags     extra_flag;
    unsigned char           extra_flag_buf[2];

    readn (sockfd, extra_flag_buf, 2);
    /* 1-bit reserved for future */
    extra_flag.reserved = hex2dec (extra_flag_buf, 2) & (0x0001 << 15);
    /* 1-bit skip-worktree flag (used by sparse checkout) */
    extra_flag.skip_worktree = hex2dec (extra_flag_buf, 2) & (0x0001 << 14);
    /* 1-bit intent-to-add flag (used by "git add -N") */
    extra_flag.intent_to_add = hex2dec (extra_flag_buf, 2) & (0x0001 << 13);
    /* 13-bit unused, must be zero */
    extra_flag.unused = hex2dec (extra_flag_buf, 2) & (0xFFFF >> 3);
    assert (extra_flag.unused == 0);
    *entry_len += 2;
}

void
parse_http_url (char *http_url, struct url_combo *url_combo)
{
    ptrdiff_t     protocol_len;

    protocol_len = strchr (http_url, '/') - http_url + 2;
    strncpy (url_combo->protocol, http_url, protocol_len);
    url_combo->protocol[protocol_len] = '\0';
    assert (!strcmp (url_combo->protocol, "http://")
            || !strcmp (url_combo->protocol, "https://"));
    url_combo->uri = strchr (http_url + protocol_len, '/');
    strncpy (url_combo->host, http_url + protocol_len,
            url_combo->uri - (http_url + protocol_len));
    url_combo->host[url_combo->uri - (http_url + protocol_len)] = '\0';
}

void
setnonblocking (int sockfd)
{
    int     opts;

    opts = fcntl (sockfd, F_GETFL);
    if (opts < 0) {
        perror ("fcntl(sock,GETFL)");
        exit (-1);
    }

    opts |= O_NONBLOCK;
    if (fcntl (sockfd, F_SETFL, opts) < 0) {
        perror ("fcntl(sock,SETFL,opts)");
        exit (-1);
    }
}

void
setblocking (int sockfd)
{
    int     opts;

    opts = fcntl (sockfd, F_GETFL);
    if (opts < 0) {
        perror("fcntl(sock,GETFL)");
        exit (-1);
    }

    opts &=  ~O_NONBLOCK;
    if (fcntl (sockfd, F_SETFL, opts) < 0) {
        perror ("fcntl(sock,SETFL,opts)");
        exit (-1);
    }
}

ssize_t
writen (int fd, const void *vptr, size_t n)
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

ssize_t
readline (int fd, void *vptr, size_t maxlen)
{
    size_t  n;
    ssize_t rc;
    char    c,*ptr;

    ptr = vptr;
    for (n = 1; n < maxlen; n++) {
    again:
        if ( ( rc = read (fd, &c , 1)) == 1) {
            *ptr++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            *ptr = 0;
            return n - 1;
        } else {
            if (errno == EINTR)
                goto again;
            return -1;
        }
    }

    *ptr = 0;
    return n;
}

ssize_t
readn (int fd, void *vptr, size_t n)
{
    char    *ptr;
    size_t  nleft;
    ssize_t nread;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nread = read (fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0; /*and call read() again*/
            else
                return(-1);
        } else if (nread == 0)
            break;          /*EOF*/
        nleft -= nread;
        ptr   += nread;
    }
    return n - nleft;
}

void
touch_file_et (http_res_t *response, const char *filename, size_t filesize)
{
    unsigned char   *text;
    unsigned long    tlen;
    char            *blob_header;
    char             filepath[BUFFER_SIZE * 10] = { '\0' };

    if (!filesize) {
        return;
    }
    strncat (filepath, filename, BUFFER_SIZE * 10 - 1);

    blob_header = (char *) malloc (BLOB_MAX_LEN + 1);
    snprintf(blob_header, BLOB_MAX_LEN + 1, "blob %ld", filesize);

    tlen = filesize + strlen (blob_header) + 1;
    text = (unsigned char *) malloc (tlen);

    if (uncompress (text, &tlen, response->content,
                    response->content_len) != Z_OK) {
        printf ("%s " ESC "[31m[FAILED]" ESC "[0m\n", filename);
        free (text);
        return;
    }

    printf ("%s " ESC "[35m[OK]" ESC "[0m\n", filename);

    FILE *file = fopen (filename, "wb+");
    /* skip write blob header */
    if ( (fwrite (text + strlen(blob_header) + 1, 1, filesize,
                  file)) != filesize) {
        printf("frite error");
        return;
    }

    fclose (file);
    free (text);
    free (blob_header);
}

int
create_dir (const char *sPathName)
{
    size_t  i;
    size_t  len;
    char    DirName[256] = {'\0'};

    strncpy (DirName, sPathName, 255);
    len = strlen (DirName);
    if (DirName[len - 1] != '/')
        strcat (DirName, "/");
    len = strlen (DirName);
    for (i = 1; i < len; i++) {
        if (DirName[i] == '/') {
            DirName[i] = 0;
            if (access (DirName, F_OK) == -1) {
                if (mkdir (DirName, 0755) == -1) {
                    return -1;
                }
            }
            DirName[i] = '/';
        }
    }
    return 0;
}

void
create_all_path_dir (ce_body_t ce_bd)
{
    char    *result;
    char    dir[BUFFER_SIZE] = { '\0' };

    result = strrchr (ce_bd->name, '/');
    if (result) {
        ptrdiff_t dis = result - ce_bd->name;
        if (dis > BUFFER_SIZE) {
            printf ("pathname is too long");
            exit (-1);
        }

        strncpy (dir, ce_bd->name, dis);
        if (create_dir (dir) == -1) {
            perror ("mkdir error ");
            exit (-1);
        }
    }
}

int
process_data (void *buffer, size_t size, size_t nmemb, void *stream)
{
    body_t bd = (body_t)stream;
    size_t buffer_size = size * nmemb;

    /*if realloc first arg is NULL and second arg is not NULL, it's equals malloc()*/
    bd->content = (unsigned char *) realloc (bd->content, buffer_size + bd->lenght);
    if (bd->content == NULL) {
        fprintf(stderr, "realloc memory fail\n");
        return -1;
    }

    memcpy ((void *)(bd->content + bd->lenght), buffer, buffer_size);
    bd->lenght += buffer_size;

    return buffer_size;
}


void
task_func (void *arg)
{
    body         bd;
    CURL        *curl;
    CURLcode     res;
    size_t       filesize;
    char        *filename;
    size_t       retcode;
    char         object_url[BUFFER_SIZE] = {'\0'};

    ce_body_t ce_body = (ce_body_t) arg;
    filesize = hex2dec ((ce_body->entry_body->size), 4);
    filename = ce_body->name;

    concat_object_url (ce_body->entry_body, object_url);
    if (object_url[0] == '\0') {
        return;
    }

    bd.content = (unsigned char *) malloc (1);
    bd.lenght  = 0;

    curl  = curl_easy_init();
    if (curl) {
       curl_easy_setopt(curl, CURLOPT_URL, object_url);
        /* example.com is redirected, so we tell libcurl to follow redirection */
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&bd);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &process_data);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\t%s\n",
                            curl_easy_strerror(res),
                            object_url);
            return;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE , &retcode);
        if (retcode == 200 && bd.lenght > 0) {
            size_t          tlen;
            unsigned char   *text;
            char            *blob_header;

            blob_header = (char *) malloc (BLOB_MAX_LEN + 1);
            snprintf(blob_header, BLOB_MAX_LEN + 1, "blob %ld", filesize);

            tlen = filesize + strlen (blob_header) + 1;
            text = (unsigned char *) malloc (tlen);

            if (uncompress (text, &tlen, bd.content,
                            bd.lenght) != Z_OK) {
                printf ("%s " ESC "[31m[FAILED]" ESC "[0m\n", filename);
                free (blob_header);
                free (text);
                return;
            }

            printf ("%s " ESC "[35m[OK]" ESC "[0m\n", filename);
            /* skip write blob header */
            FILE* file = fopen (filename, "w");
            if ( (fwrite (text + strlen(blob_header) + 1, 1, filesize,
                          file)) != filesize) {
                printf("frite error");
            }
            fclose (file);
            free (blob_header);
            free (text);
            free (bd.content);
            /* always cleanup */
            curl_easy_cleanup(curl);
        }
    } else {
        fprintf(stderr, "curl init error.\n");
    }
    free (ce_body->name);
    free (ce_body->entry_body);
    free (ce_body);
}

void
parse_index_object (int sockfd)
{
    int             ent_num, j;
    magic_hdr       magic_head;
    ce_body_t       ce_bd;
    size_t          namelen;
    entry_body_t    entry_bd;
    struct _flags   file_flags;
    int             entry_len;

    init_check (sockfd, &magic_head);
    ent_num = hex2dec (magic_head.file_num, 4);

    printf("find %d files, downloading~\n", ent_num);

    threadpool thpool = thpool_init(10);

    for (j = 0; j < ent_num; j++) {
        entry_len = ENTRY_SIZE;

        entry_bd  = (entry_body_t ) malloc (sizeof (entry_body));
        readn (sockfd, entry_bd, sizeof(entry_body));

        file_flags.assume_valid = hex2dec (entry_bd->ce_flags, 2) & (0x0001 << 15);
        file_flags.extended = hex2dec (entry_bd->ce_flags, 2) & (0x0001 << 14);
        if(hex2dec(magic_head.version, 4) == 2) {
            assert(file_flags.extended == 0);
        }
        file_flags.stage.stage_one =
            hex2dec (entry_bd->ce_flags, 2) & (0x0001 << 13);
        file_flags.stage.stage_two =
            hex2dec (entry_bd->ce_flags, 2) & (0x0001 << 12);

        if (file_flags.extended && hex2dec (magic_head.version, 4) >= 3) {
            handle_version3orlater (sockfd, &entry_len);
        }

        ce_bd = (ce_body_t) malloc(sizeof (ce_body));
        namelen = hex2dec (entry_bd->ce_flags, 2) & (0xFFFF >> 4);
        ce_bd->name = get_name (sockfd, namelen, &entry_len);
        ce_bd->entry_len = entry_len;
        pad_entry (sockfd, ce_bd->entry_len);

        ce_bd->entry_body = entry_bd;

        create_all_path_dir (ce_bd);

        thpool_add_work (thpool, (void*) task_func, (void*) ce_bd);
    }

    thpool_wait(thpool);
    thpool_destroy(thpool);
}

int
strip_http_header (int sockfd)
{
    char         ch;
    ssize_t      ret;
    int          i = 0;

    while ((ret = read (sockfd, &ch, 1)) != 0) {
        if(ret < 0) {
            if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN){
                continue;
            }else{
                perror("read");
                exit(-1);
            }
        }
        if (ch == '\r' || ch == '\n'){
            if (++i == 4)
                return sockfd;
        }
        else{
            i = 0;
        }
    }
    return -1;
}

int
force_rm_dir(const char *path)
{
    DIR     *d;
    size_t  path_len, len;
    int     r = -1, r2 = -1;
    struct  dirent *p;
    char    *buf;

    d = opendir (path);
    path_len = strlen (path);
    if (d) {
        r = 0;
        while (!r && (p = readdir (d))) {
            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp (p->d_name, ".") || !strcmp (p->d_name, "..")) {
                continue;
            }
            len = path_len + strlen(p->d_name) + 2;
            buf = malloc (len);
            if (buf) {
                struct stat statbuf;
                snprintf (buf, len, "%s/%s", path, p->d_name);
                if (!stat (buf, &statbuf)) {
                    if (S_ISDIR (statbuf.st_mode))
                        r2 = force_rm_dir (buf);
                    else
                        r2 = unlink (buf);
                }
                free (buf);
            }
            r = r2;
        }
        closedir (d);
    }
    if (!r)
        r = rmdir (path);
    return r;
}


void
mk_dir (char *path)
{
    char    c;

    if (access (path, F_OK) == 0) {
        /*force remote dir*/
        printf ("please input y(yes) to force remove exists dir or n(no) to exit process to continue. ");

        c = (char)getchar();
        if(c != 'y') {
            printf("process exit");
            exit(0);
        }

        printf("force remove exists dir %s\n", path);
        force_rm_dir (path);
        printf("remove dir finish\n");
    }

    if (mkdir (path, 0755) == -1) {
        perror ("mkdir error");
        exit (-1);
    }
}

void
concat_object_url (entry_body_t entry_bd, char *object_url)
{
    char    *hex_name;

    hex_name = sha12hex (entry_bd->sha1);

    if (strlen (hex_name) == 40) {
        snprintf (object_url, BUFFER_SIZE, "%s%s%sobjects/%2.2s/%s",
                  url_combo.protocol, url_combo.host, url_combo.uri, hex_name , hex_name + 2);
    }

    free(hex_name);
}

bool
check_argv (int argc, char *argv[])
{
    int    opt;

    if (argc < 2) {
        goto end;
    }

    while ( (opt = getopt (argc, argv, ":u:p:")) != -1) {
        switch (opt) {
            case 'u':
                url = optarg;
                break;
            case 'p':
                port = validate_port (atoi (optarg));
                break;
            default:
                goto end;
        }
    }

    if (url != NULL) {
        return true;
    }
end:
    printf("Usage: %s <-u url> [-p port]\n", argv[0]);
    return false;
}

int
main (int argc, char *argv[])
{
    int          index_sockfd;
    char         index_uri[2048];
    http_des_t   des;

    if (check_argv (argc, argv) == false)
        exit(-1);

    parse_http_url (url, &url_combo);

    mk_dir (url_combo.host);
    assert (chdir (url_combo.host) == 0);

    get_ip_from_host (ip, url_combo.host, 128);

    des.host_name = url_combo.host;
    des.host_port = port;
    snprintf (index_uri, 2048, "%s%s", url_combo.uri, "index");
    des.uri = index_uri;

    index_sockfd = http_get (&des);

    if (strip_http_header (index_sockfd) != index_sockfd) {
        exit(-1);
    }
    parse_index_object (index_sockfd);


    return 0;
}
