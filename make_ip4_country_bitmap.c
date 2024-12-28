#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// bitmap array
uint8_t bitmap[1 << 21] = {};

// forward declarations
static bool parse_line(unsigned line_number, char* line, int len,
                       uint32_t* first_ip, uint32_t* last_ip, char* country_code);
static bool country_code_match(char* code, char* country_codes);

int main(int argc, char* argv[])
{
    if (argc != 4) {
        char* progname = strrchr(argv[0], '/');
        if (progname) {
            progname++;
        } else {
            progname = argv[0];
        }
        fprintf(stderr, "Usage: %s <input.csv> <output-filename> <country-codes>\n", progname);
        fputc('\n', stderr);
        fputs("    where <country-codes> is a comma-separated list of 2-char country codes,\n", stderr);
        fputs("    zz, -, or -- means undefined country\n", stderr);
        return 1;
    }

    // convert country codes to upper case
    for (int i = 0, n = strlen(argv[3]); i < n; i++) {
        argv[3][i] = (char) toupper(argv[3][i]);
    }

    // read input CSV file and build map
    int fd_in = open(argv[1], O_RDONLY);
    if (fd_in == -1) {
        perror(argv[1]);
        return 1;
    } else {
        unsigned line_number = 1;
        char line_buffer[256];
        int data_len = 0;  // length of data in the buffer
        bool eof = false;
        while (data_len || !eof) {
            // find end of line
            char* lf = nullptr;
            if (data_len) {
                lf = memchr(line_buffer, '\n', data_len);
            }
            if (!lf) {
                // no data in the buffer or the line is incomplete
                // read more data
                int n = sizeof(line_buffer) - data_len;
                if (n == 0) {
                    // line does not fit into buffer
                    fprintf(stderr, "CSV line %u too long\n", line_number);
                    close(fd_in);
                    return 1;
                }
                ssize_t bytes_read = read(fd_in, &line_buffer[data_len], n);
                if (bytes_read == -1) {
                    perror(argv[1]);
                    close(fd_in);
                    return 1;
                }
                if (bytes_read < n) {
                    eof = true;
                }
                data_len += bytes_read;
                // need to rescan for LF char
                continue;
            }

            // parse line
            int line_len = 1 + (int) (lf - line_buffer);
            uint32_t first_ip, last_ip;
            char country_code[2];
            if (parse_line(line_number, line_buffer, line_len, &first_ip, &last_ip, country_code)) {
                if (country_code_match(country_code, argv[3])) {
                    //printf("%u - %u: %c%c -- MATCH\n", first_ip, last_ip, country_code[0], country_code[1]);
                    // update bitmap
                    for (uint32_t index = first_ip; index < last_ip; index++) {
                        bitmap[index >> (8 + 3)] |= 1 << ((index >> 8) & 7);
                    }
                    bitmap[last_ip >> (8 + 3)] |= 1 << ((last_ip >> 8) & 7);
                }
                //else {printf("%u - %u: %c%c\n", first_ip, last_ip, country_code[0], country_code[1]);}
            }

            // next line
            int tail_len = sizeof(line_buffer) - line_len;
            if (tail_len) {
                memmove(line_buffer, &line_buffer[line_len], tail_len);
            }
            data_len -= line_len;
            line_number++;
        }
        close(fd_in);
    }

    // write bitmap
    int fd_out = open(argv[2], O_CREAT | O_WRONLY, 0644);
    if (fd_out == -1) {
        perror(argv[2]);
        return 1;
    }
    ssize_t bytes_written = write(fd_out, bitmap, sizeof(bitmap));
    if (bytes_written == -1) {
        perror(argv[2]);
        close(fd_out);
        return 1;
    }
    close(fd_out);

    if (bytes_written != sizeof(bitmap)) {
        fputs("Cannot write output file\n", stderr);
        return 1;
    }
    return 0;
}

static bool parse_csv_ipaddr(unsigned line_number, char* start, int len, uint32_t* ip4_addr, int* field_len)
/*
 * parse CSV field containing IP address
 */
{
    if (len == 0) {
        fprintf(stderr, "Malformed CSV line %u: IP address expected\n", line_number);
        return false;
    }

    char* end;
    char quote = *start;
    if (quote == '"' || quote == '\'') {
        start++;
        end = memchr(start, quote, len - 1);
        if (!end) {
            fprintf(stderr, "Malformed CSV line %u: no closing quote\n", line_number);
            return false;
        }
        *end++ = 0;
        if (*end != ',') {
            fprintf(stderr, "Malformed CSV line %u: comma expected\n", line_number);
            return false;
        }
        *field_len = 2 + (int) (end - start);
    } else {
        end = memchr(start, ',', len);
        if (!end) {
            fprintf(stderr, "Malformed CSV line %u: comma expected\n", line_number);
            return false;
        }
        *end = 0;
        *field_len = 1 + (int) (end - start);
    }

    if (end - start == 0) {
        fprintf(stderr, "Malformed CSV line %u: IP address expected\n", line_number);
        return false;
    }

    long n = strtol(start, &end, 10);
    if (*end == 0) {
        *ip4_addr = (uint32_t) n;
        return true;
    }

    uint32_t ip = 0;
    for (int i = 0; i < 3; i++) {
        if (*end != '.') {
            fprintf(stderr, "Malformed CSV line %u: bad IP address\n", line_number);
            return false;
        }
        if (n > 255) {
            fprintf(stderr, "Malformed CSV line %u: bad IP address\n", line_number);
            return false;
        }
        ip <<= 8;
        ip |= (uint32_t) n;

        start = end + 1;
        n = strtol(start, &end, 10);
    }
    if (n > 255 || *end != 0) {
        fprintf(stderr, "Malformed CSV line %u: bad IP address\n", line_number);
        return false;
    }
    ip <<= 8;
    ip |= (uint32_t) n;
    *ip4_addr = ip;

    return true;
}

static bool parse_line(unsigned line_number, char* line, int len,
                       uint32_t* first_ip, uint32_t* last_ip, char* country_code)
{
    int field_len;

    if (!parse_csv_ipaddr(line_number, line, len, first_ip, &field_len)) {
        return false;
    }
    line += field_len;
    len -= field_len;

    if (!parse_csv_ipaddr(line_number, line, len, last_ip, &field_len)) {
        return false;
    }
    line += field_len;
    len -= field_len;

    if (len == 0) {
        fprintf(stderr, "Malformed CSV line %u: country code expected\n", line_number);
        return false;
    }
    if (*line == '"' || *line == '\'') {
        line++;
        len--;
    }
    if (!len) {
        fprintf(stderr, "Malformed CSV line %u: country code expected\n", line_number);
        return false;
    }

    country_code[0] = toupper(line[0]);
    if (line[0] == '-') {
        country_code[1] = '-';
    } else if (len > 1) {
        country_code[1] = toupper(line[1]);
    } else {
        fprintf(stderr, "Malformed CSV line %u: bad country code\n", line_number);
        return false;
    }
    return true;
}

static bool country_code_match(char* code, char* country_codes)
{
    char* p = country_codes;
    for (;;) {
        char c = *p++;
        if (c == 0) {
            return false;
        }
        if (c == ',') {
            continue;
        }
        if (c == '-' || (c == 'Z' && *p == 'Z')) {
            if ((code[0] == 'Z' && code[1] == 'Z') || code[0] == '-') {
                return true;
            }
        } else if (c == code[0] && *p == code[1]) {
            return true;
        }
        // skip to the next country code
        for (;;) {
            char c = *p++;
            if (c == 0) {
                return false;
            }
            if (c == ',') {
                break;
            }
        }
    }
}
