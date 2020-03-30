#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_BUFF_SIZE 255
#define IPV4_STR_SIZE 16
#define IPV6_STR_SIZE 40

enum TYPE {
    TYPE_A = 1,
    TYPE_NS,
    TYPE_MD,
    TYPE_MF,
    TYPE_CNAME,
    TYPE_SOA,
    TYPE_MB,
    TYPE_MG,
    TYPE_MR,
    TYPE_NULL,
    TYPE_WKS,
    TYPE_PTR,
    TYPE_HINFO,
    TYPE_MINFO,
    TYPE_MX,
    TYPE_TXT,
    TYPE_AAAA = 28,
};

enum CLASS {
    CLASS_IN = 1,
    CLASS_CS,
    CLASS_CH,
    CLASS_HS,
};

struct dns_header{
    uint16_t	id :16;		/* identification number */
#if BYTE_ORDER == LITTLE_ENDIAN || BYTE_ORDER == PDP_ENDIAN
    uint16_t	rd :1;		/* recursion desired */
    uint16_t	tc :1;		/* truncated message */
    uint16_t	aa :1;		/* authoritative answer */
    uint16_t	opcode :4;	/* type of query */
    uint16_t	qr :1;		/* query or response */
    uint16_t	rcode :4;	/* response code */
    uint16_t	cd: 1;		/* checking disabled */
    uint16_t	ad: 1;		/* authentic data */
    uint16_t	unused :1;
    uint16_t	ra :1;		/* recursion available */
#endif
#if BYTE_ORDER == BIG_ENDIAN
#ifdef BYTE_ORDER
	uint16_t	qr: 1;		/* query or response */
	uint16_t	opcode: 4;	/* type of query */
	uint16_t	aa: 1;		/* authoritative answer */
	uint16_t	tc: 1;		/* truncation */
	uint16_t	rd: 1;		/* recursion desired */
	uint16_t	ra: 1;		/* recursion available */
	uint16_t	unused :1;
	uint16_t	ad: 1;      /* authentic data */
	uint16_t	cd: 1;		/* checking disabled */
	uint16_t	rcode :4;	/* response code */
#endif
#endif

    uint16_t	qdcount :16;	/* number of question entries */
    uint16_t	ancount :16;	/* number of answer entries */
    uint16_t	nscount :16;	/* number of authority entries */
    uint16_t	arcount :16;	/* number of resource entries */
};

struct dns_question_info {
    uint16_t qtype;     /* type of the query (enum TYPE) */
    uint16_t qclass     /* query class (enum CLASS) */;
};

struct buffer {
    char *data;
    uint32_t pos;
};

/* Custom function for counting size of the name. I needed this because for some reason whoever
 * made name compression decided that after a pointer there will be no 0 ending byte,
 * which makes very hard to count how many bytes of space it occupies, without function like this */
size_t namelen(const char *name);

struct sockaddr_in * get_dest_server(char *hostname, int32_t server_port);

void init_buffer(struct buffer *buff);
void empty_buffer(struct buffer *buff);

void add_dns_header(struct buffer *buff, int id, bool reverse, bool recursive);
void add_question(struct buffer *buff, char *hostname, bool ipv6);
int add_reverse_question(struct buffer *buff, char *address);

static char * encode_hostname(char *hostname);
static char * decode_name(struct buffer *buff, char *name);

void print_questions(struct buffer *buff, int32_t count);
static void print_resource(struct buffer *buff, int32_t count);

char * buff_to_hostname(struct buffer *buff);
char * buff_to_type(struct buffer *buff, enum TYPE *type);
char * buff_to_class(struct buffer *buff, enum CLASS *class);
uint32_t buff_to_int32(struct buffer *buff);
uint16_t buff_to_rdlength(struct buffer *buff);
char * print_rdata(struct buffer *buff, enum TYPE type, enum CLASS class, uint16_t rdlength);

char hex_to_char(uint8_t hex);
size_t namelen(const char *name);
uint16_t pointer_to_offset(const char *bytes);

extern inline void print_input_error(char *program_name)
{
    fprintf(stderr, "Usage: %s [-r] [-x] [-6] -s server [-p port] address\n", program_name);
}

extern inline bool isPointer(uint8_t c)
{
    return (c >> 6u) == 0x3;
}
