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
#include <assert.h>

#include "dns-resolver.h"

int main(int argc, char *argv[])
{
    int32_t ret;
    int32_t opt;
    bool recursive = false, reverse = false, ipv6 = false;
    char *hostname;

    struct sockaddr_in *server;
    char *server_hostname = NULL;
    int32_t server_port = 0;

    int32_t id = 0;
    int32_t socket_desc = 0;

    struct buffer datagram;

    /* Arguments parsing */
    while ((opt = getopt(argc, argv, "hrx6s:p:")) != -1) {
        switch (opt) {
            case 'r':
                recursive = true;
                break;
            case 'x':
                reverse = true;
                break;
            case '6':
                ipv6 = true;
                break;
            case 's':
                /* get an IP of a DNS server */
                server_hostname = optarg;
                if (!server_hostname) {
                    print_input_error(argv[0]);
                    return -1;
                }
                break;
            case 'p':
                server_port = (int) strtol(optarg, NULL, 10);
                break;
            case 'h':
                if (argc != 2) {
                    print_input_error(argv[0]);
                    return -1;
                }
                printf("Program for sending DNS queries to a DNS server and receiving it's response"
                       " in readable format.\n");
                printf("dns [-r] [-x] [-6] -s server [-p port] address\n");
                printf("\n");
                printf("-r:\t\trecursive query\n");
                printf("-x:\t\treverse query\n");
                printf("-6:\t\tquery for AAAA record\n");
                printf("-s:\t\tserver where to send query\n");
                printf("-p:\t\tport, on which to send a query (default  53)\n");
                printf("address:\taddress of which to ask a server\n");
                return 0;
            default:
                print_input_error(argv[0]);
                return -1;
        }
    }

    /* There must be one non-option argument left for address */
    if (optind + 1 != argc) {
        print_input_error(argv[0]);
        return -1;
    }
    /* Then the last argument must be the hostname for a query */
    hostname = argv[optind];

    /* Assign a socket */
    socket_desc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_desc == -1) {
        fprintf(stderr, "Couldn't assign a socket:\n%d %s\n", errno, strerror(errno));
        return -1;
    }

    /* Fill in destination server info into a socket structure */
    server = get_dest_server(server_hostname, server_port);

    /* Set a timeout of 5 seconds for connection with provided server */
    /* Implementation of timeout used from https://stackoverflow.com/questions/2876024/linux-is-there-a-read-or-recv-from-socket-with-timeout */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    /* Use connected UDP socket method (for server checking server availability) */
    ret = connect(socket_desc, (struct sockaddr *)server, sizeof(*server));
    if (ret == -1) {
        fprintf(stderr, "Couldn't connect to the server:\n%d %s\n", errno, strerror(errno));
        return ret;
    }

    /* Initialize buffer */
    init_buffer(&datagram);

    /* Generate identifier which will be used to identify dns packets */
    id = htons(getpid());

    /* Fill the DNS Header into buffer */
    add_dns_header(&datagram, id, reverse, recursive);

    /* Question data */
    if (reverse)
        ret = add_reverse_question(&datagram, hostname);
    else
        add_question(&datagram, hostname, ipv6);

    if (ret == -1) {
        return ret;
    }

    /* Send data */
    ret = send(socket_desc, datagram.data, datagram.pos, 0);
    if (ret == -1) {
        fprintf(stderr, "Couldn't send a datagram:\n%d %s\n", errno, strerror(errno));
        return ret;
    }

    /* Initialize the buffer again for receiving data back */
    empty_buffer(&datagram);

    /* Receive data */
    ret = recv(socket_desc, datagram.data, MAX_BUFF_SIZE, 0);
    if (ret <= 0) {
        fprintf(stderr, "Couldn't receive reply from the server:\n%d %s\n", errno, strerror(errno));
        return ret;
    }

    struct dns_header *header = (struct dns_header *)&datagram.data[datagram.pos];
    datagram.pos += sizeof(*header);

    if (id != header->id) {
        printf("TODO ID\n");
        return 1;
    }

    printf("Authoritative: %s, ", header->aa ? "Yes" : "No");
    printf("Recursive: %s, ", header->rd && header->ra ? "Yes" : "No");
    printf("Truncated: %s\n", header->tc ? "Yes" : "No");
    printf("Question section (%d)\n", ntohs(header->qdcount));
    print_questions(&datagram, ntohs(header->qdcount));
    printf("Answer section (%d)\n", ntohs(header->ancount));
    print_resource(&datagram, ntohs(header->ancount));
    printf("Authority section (%d)\n", ntohs(header->nscount));
    print_resource(&datagram, ntohs(header->nscount));
    printf("Additional section (%d)\n", ntohs(header->arcount));
    print_resource(&datagram, ntohs(header->arcount));

    free(datagram.data);

    return 0;
}

/* Function for converting standard dotted hostname format to network format
 * www.google.com\0 would be \3www\6google\3com\0 */
static char * encode_hostname(char *hostname)
{
    unsigned subpart = 0; /* tells us where next hostname part starts */
    unsigned pos, hostname_len = strlen(hostname) + 1;
    char *qname = malloc(hostname_len + 1);

    for (pos = 0; pos < hostname_len; pos++) {
        if (hostname[pos] == '.' || hostname[pos] == '\0') {
            /* if we meet dot or zero byte that means we are at the end of the hostname subpart
             * and we save it's length (position - base) */
            qname[subpart] = (char)(pos - subpart);
            /* copy the subpart */
            strncpy(&qname[subpart + 1], &hostname[subpart], pos - subpart);
            if (hostname[pos] == '\0')
                break;
            subpart = pos + 1; /* update the base to where hostname part ended and skip the dot */
        }
    }
    qname[hostname_len] = '\0';

    return qname;
}


size_t namelen(const char *name)
{
    size_t pos;
    for (pos = 0; name[pos] != '\0'; pos++) {
        if (isPointer(name[pos]))
            return pos + 2;
    }
    return pos + 1;
}

uint16_t pointer_to_offset(const char *bytes)
{
    uint16_t offset;
    offset  = (uint16_t)(bytes[0] & 0x3f) << 8u;
    offset |= (uint8_t)bytes[1];
    return offset;
}

char * buff_to_hostname(struct buffer *buff)
{
    char *hostname = decode_name(buff, &buff->data[buff->pos]);
    buff->pos += namelen(&buff->data[buff->pos]);
    return hostname;
}

// Function for converting network format of hostname into the standard dotted format */
static char * decode_name(struct buffer *buff, char *name)
{
    uint16_t pos = 0;
    unsigned char next_length = 0;
    uint16_t name_length = strlen(name) + 1;

    /* Alloc the approx size (if we find a pointer then we will need more size */
    char *hostname = malloc(name_length);

    while(next_length < name_length) {
        next_length = name[next_length];
        if (isPointer(next_length)) {
            /* If 2 most significant bits are both 1, it is a pointer, not a length
             * and next 14 bits are offset */
            uint16_t offset = pointer_to_offset(&name[pos]); // Calculate and offset
            char * pointed_name = strdup(&buff->data[offset]); // Copy the content from offset
            char * decompressed_name = decode_name(buff, pointed_name); // Decode the content
            int16_t additional_len = strlen(decompressed_name);
            hostname = realloc(hostname, name_length + additional_len); // Reallocate the string

            memcpy(&hostname[pos], decompressed_name, additional_len + 1); // Copy the decoded content to string
            name_length += additional_len; // Update the length
            break;
        }
        assert(next_length < name_length);

        memcpy(&hostname[pos], &name[pos + 1], next_length); // Copy next part
        pos += next_length; // Update position
        hostname[pos++] = '.'; // Add .
        next_length = pos; // Update next length
    }
    hostname[name_length - 1] = '\0';
    return hostname;
}

char * buff_to_type(struct buffer *buff, enum TYPE *type)
{
    uint16_t *type_addr = (uint16_t *)&buff->data[buff->pos];
    if (!type)
        type = alloca(sizeof(enum TYPE));

    *type = ntohs(*type_addr);
    char *str;

    switch (*type) {
        case TYPE_A:
            str = strdup("A");
            break;
        case TYPE_NS:
            str = strdup("NS");
            break;
        case TYPE_MD:
            str = strdup("MD");
            break;
        case TYPE_MF:
            str = strdup("MF");
            break;
        case TYPE_CNAME:
            str = strdup("CNAME");
            break;
        case TYPE_SOA:
            str = strdup("SOA");
            break;
        case TYPE_MB:
            str = strdup("MB");
            break;
        case TYPE_MG:
            str = strdup("MG");
            break;
        case TYPE_MR:
            str = strdup("MR");
            break;
        case TYPE_NULL:
            str = strdup("NULL");
            break;
        case TYPE_WKS:
            str = strdup("WKS");
            break;
        case TYPE_PTR:
            str = strdup("PTR");
            break;
        case TYPE_HINFO:
            str = strdup("HINFO");
            break;
        case TYPE_MINFO:
            str = strdup("MINFO");
            break;
        case TYPE_MX:
            str = strdup("MX");
            break;
        case TYPE_TXT:
            str = strdup("TXT");
            break;
        case TYPE_AAAA:
            str = strdup("AAAA");
            break;
        default:
            fprintf(stderr, "Warning: unhandled TYPE value (%d)\n", *type);
            str = strdup("Unknown");
    }
    buff->pos += sizeof(uint16_t);
    return str;
}

char * buff_to_class(struct buffer *buff, enum CLASS *class)
{
    uint16_t *class_addr = (uint16_t *)&buff->data[buff->pos];
    if (!class)
        class = alloca(sizeof(enum CLASS));

    *class = ntohs(*class_addr);
    char *str;

    switch (*class) {
        case CLASS_IN:
            str = strdup("IN");
            break;
        case CLASS_CS:
            str = strdup("CS");
            break;
        case CLASS_CH:
            str = strdup("CH");
            break;
        case CLASS_HS:
            str = strdup("HS");
            break;
        default:
            fprintf(stderr, "Warning: unhandled CLASS value (%d)\n", *class);
            str = strdup("Unknown");
    }
    buff->pos += sizeof(uint16_t);
    return str;
}

void print_questions(struct buffer *buff, int32_t count)
{
    int32_t i;
    for(i = 0; i < count; i++) {

        char *hostname = buff_to_hostname(buff);
        if (hostname == NULL)
            return;
        char *type = buff_to_type(buff, NULL);
        char *class = buff_to_class(buff, NULL);

        printf("\t%s, %s, %s\n", hostname, type, class);
    }
}

uint32_t buff_to_int32(struct buffer *buff)
{
    uint32_t *num = (uint32_t  *)&buff->data[buff->pos];
    buff->pos += sizeof(uint32_t);
    return ntohl(*num);
}

uint16_t buff_to_rdlength(struct buffer *buff)
{

    uint16_t *rdlength = (uint16_t  *)&buff->data[buff->pos];
    buff->pos += sizeof(uint16_t);
    return ntohs(*rdlength);;
}


char * print_rdata(struct buffer *buff, enum TYPE type,
                   enum CLASS class, uint16_t rdlength)
{
    char *rdata = NULL;
    char *buffer = &buff->data[buff->pos];

    switch(type) {
        case TYPE_A:
            rdata = malloc(IPV4_STR_SIZE);
            if (!inet_ntop(AF_INET, buffer, rdata, IPV4_STR_SIZE))
                fprintf(stderr, "inet_ntop: %d %s\n", errno, strerror(errno));
            printf("%s", rdata);
            buff->pos += rdlength;
            break;
        case TYPE_MINFO:
            rdata = buff_to_hostname(buff);
            printf("\n\t\tresponsible mailbox: %s\n", rdata);
            rdata = buff_to_hostname(buff);
            printf("\n\t\terrors mailbox: %s", rdata);
            break;
        case TYPE_CNAME:
        case TYPE_NS:
        case TYPE_MR:
        case TYPE_MG:
        case TYPE_MF:
        case TYPE_MD:
        case TYPE_MB:
        case TYPE_PTR:
            rdata = buff_to_hostname(buff);
            printf("%s", rdata);
            break;
        case TYPE_AAAA:
            rdata = malloc(IPV6_STR_SIZE);
            if (!inet_ntop(AF_INET6, buffer, rdata, IPV6_STR_SIZE))
                fprintf(stderr, "inet_ntop: %d %s\n", errno, strerror(errno));
            printf("%s", rdata);
            buff->pos += rdlength;
            break;
        case TYPE_SOA: {
            char *mname = buff_to_hostname(buff);
            char *rname = buff_to_hostname(buff);
            int serial = buff_to_int32(buff);
            int refresh = buff_to_int32(buff);
            int retry = buff_to_int32(buff);
            int expire = buff_to_int32(buff);
            int minimum = buff_to_int32(buff);

            printf("\n\t\t");
            printf("primary server name: %s\n\t\t"
                   "responsible authority's mailbox: %s\n\t\t"
                   "serial number: %d\n\t\t"
                   "refresh interval: %d\n\t\t"
                   "retry interval: %d\n\t\t"
                   "expire limit: %d\n\t\t"
                   "minimum TTL: %d",
                   mname, rname, serial, refresh, retry, expire, minimum);
            break;
        }
        case TYPE_TXT:
            printf("%s", buffer);
            buff->pos += strlen(buffer) + 1;
            break;
        case TYPE_HINFO:
            printf("\n\t\tCPU: %s", buffer);
            buff->pos += strlen(buffer) + 1;
            printf("\n\t\tOS: %s", buffer);
            buff->pos += strlen(buffer) + 1;
        default:
            fprintf(stderr, "Unhandled TYPE when parsing RDATA (%d)\n", type);
    }
    return rdata;
}


static void print_resource(struct buffer *buff, int32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        enum TYPE num_type;
        enum CLASS num_class;

        char *hostname = buff_to_hostname(buff);
        if (hostname == NULL)
            return;
        char *type = buff_to_type(buff, &num_type);
        char *class = buff_to_class(buff, &num_class);
        uint32_t ttl = buff_to_int32(buff);

        printf("\t%s, %s, %s, %d, ", hostname, type, class, ttl);
        print_rdata(buff, num_type, num_class, buff_to_rdlength(buff));
        printf("\n");
    }
}

struct sockaddr_in * get_dest_server(char *hostname, int32_t server_port)
{
    /* Get the ip from the hostname */
    struct hostent *he_server = gethostbyname(hostname);
    if (!he_server) {
        return NULL;
    }

    /* Default port is 53 */
    if (!server_port) {
        server_port = 53;
    }

    struct sockaddr_in *server = malloc(sizeof(struct sockaddr_in));

    memset(server, 0, sizeof(*server)); // Initialize everything to 0
    memcpy(&server->sin_addr, he_server->h_addr_list[0], he_server->h_length);
    server->sin_family = AF_INET;
    server->sin_port = htons(server_port);

    return server;
}

void add_dns_header(struct buffer *buff, int id, bool reverse, bool recursive)
{
    struct dns_header *header = (struct dns_header *)&buff->data[buff->pos];

    header->id = id;
    header->qr = 0;
    header->opcode = 0;
    header->aa = 0;
    header->tc = 0;
    header->rd = recursive ? 1 : 0;
    header->ra = 0;
    header->unused = 0;
    header->ad = 0;
    header->cd = 0;
    header->rcode = 0;
    header->qdcount = htons(1);
    header->ancount = 0;
    header->arcount = 0;

    /* Update buffer position */
    buff->pos += sizeof(*header);
}

void add_question(struct buffer *buff, char *hostname, bool ipv6)
{
    char *qname;
    struct dns_question_info *question;

    /* Encoding Qname and saving to buffer */
    qname = encode_hostname(hostname);
    memcpy(&buff->data[buff->pos], qname, strlen(qname)+1);
    buff->pos += strlen(qname) + 1; // Update buffer position

    /* Question info (Qtype, Qclass) */
    question = (struct dns_question_info *)&buff->data[buff->pos];
    if (ipv6)
        question->qtype = htons(TYPE_AAAA);
    else
        question->qtype = htons(TYPE_A);
    
    question->qclass = htons(CLASS_IN);

    /* Update buffer position */
    buff->pos += sizeof(struct dns_question_info);
}

char hex_to_char(uint8_t hex)
{
    return hex >= 10 ? (hex + 'a' - 10) : hex + '0';
}

/* Reverses the address and adds suffix */
int add_reverse_question(struct buffer *buff, char *address)
{
    char *qname;
    size_t qname4_length = strlen(address) + 14; /* ipv4 address + .in-addr.arpa + 0 byte is in total 14 bytes */
    size_t qname6_length = 32*2 + 9;
    struct dns_question_info *question;
    struct in6_addr addr;

    /* Inverse the address and save it into qname */
    if (inet_pton(AF_INET6, address, &addr) == 1) {
        qname = malloc(qname6_length);
        uint8_t *addr8 = addr.s6_addr;
        char c[4];
        int iter = 0;

        /* Each byte contains 2 address characters each in 4 bits, this reads them and also directly adds
         * the lengths for network hostname format */
        for (int i = 15; i >= 0; i--) {
            c[0] = 1;
            c[1] = hex_to_char(addr8[i] & 0x0f);
            c[2] = 1;
            c[3] = hex_to_char(addr8[i] >> 4);
            memcpy(&qname[iter], c, 4); // Copy the parsed byte into string
            iter += 4;
        }
        memcpy(&qname[iter], encode_hostname("ip6.arpa"), 9); // Add suffix
        memcpy(&buff->data[buff->pos], qname, qname6_length); // Copy the string into buffer
        buff->pos += qname6_length + 1; // Update buffer position
    }
    else if (inet_pton(AF_INET, address, &addr) == 1) {
        int octets[4];
        qname = malloc(qname4_length);
        /* Reverse the octets and add suffix */
        sscanf(address, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);
        sprintf(qname, "%d.%d.%d.%d.%s", octets[3], octets[2], octets[1], octets[0], "in-addr.arpa");
        memcpy(&buff->data[buff->pos], encode_hostname(qname), qname4_length + 1);
        buff->pos += qname4_length + 1; // Update buffer position
    }
    else {
        fprintf(stderr, "Could not parse a question address\n");
        return -1;
    }

    /* Question info (Qtype, Qclass) */
    question = (struct dns_question_info *)&buff->data[buff->pos];
    question->qtype = htons(TYPE_PTR); // Add question type
    question->qclass = htons(CLASS_IN); // Add question class

    /* Update buffer position */
    buff->pos += sizeof(struct dns_question_info);
    return 1;
}

void empty_buffer(struct buffer *buff)
{
    buff->pos = 0;
    memset(buff->data, 0, MAX_BUFF_SIZE);
}

void init_buffer(struct buffer *buff)
{
    buff->data = malloc(MAX_BUFF_SIZE);
    empty_buffer(buff);
}
