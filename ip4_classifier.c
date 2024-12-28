#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink_queue.h>

#include <libmnl/libmnl.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <libnftnl/set.h>


// agruments
uint32_t family;
char*    table;
char*    set_hit;
char*    set_miss;
uint32_t queue;

// bitmap array
uint8_t bitmap[1 << 21] = {};

// netlink socket
struct mnl_socket* nl_sock;
uint32_t nl_portid;


static void add_to_set(uint32_t ip_addr, char* set_name)
/*
 * Add IP address to set.
 */
{
    struct nftnl_set* set = nullptr;
    struct nftnl_set_elem* elem = nullptr;
    struct mnl_nlmsg_batch* batch = nullptr;
    struct nlmsghdr* nlh = nullptr;

    char buffer[MNL_SOCKET_BUFFER_SIZE];
    uint32_t seq = time(NULL);

    set = nftnl_set_alloc();
    if (!set) {
        goto err;
    }

    nftnl_set_set_u32(set, NFTNL_SET_FAMILY, family);
    nftnl_set_set_str(set, NFTNL_SET_TABLE,  table);
    nftnl_set_set_str(set, NFTNL_SET_NAME,   set_name);

    elem = nftnl_set_elem_alloc();
    if (elem == NULL) {
        goto err;
    }

    nftnl_set_elem_set(elem, NFTNL_SET_ELEM_KEY, &ip_addr, sizeof(ip_addr));
    nftnl_set_elem_set_u64(elem, NFTNL_SET_ELEM_TIMEOUT, 24 * 3600 * 1000);
    nftnl_set_elem_add(set, elem);

    // prepare batch

    batch = mnl_nlmsg_batch_start(buffer, sizeof(buffer));

    nftnl_batch_begin(mnl_nlmsg_batch_current(batch), seq++);
    mnl_nlmsg_batch_next(batch);

    nlh = nftnl_nlmsg_build_hdr(mnl_nlmsg_batch_current(batch),
        NFT_MSG_NEWSETELEM, family,
        NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK,
        seq++
    );
    nftnl_set_elems_nlmsg_build_payload(nlh, set);
    mnl_nlmsg_batch_next(batch);

    nftnl_batch_end(mnl_nlmsg_batch_current(batch), seq++);
    mnl_nlmsg_batch_next(batch);

    // send batch

    if (mnl_socket_sendto(nl_sock, mnl_nlmsg_batch_head(batch),
                          mnl_nlmsg_batch_size(batch)) < 0) {
        goto err;
    }

    mnl_nlmsg_batch_stop(batch);

    for (;;) {
        int ret = mnl_socket_recvfrom(nl_sock, buffer, sizeof(buffer));
        if (ret == -1) {
            goto err;
        }
        ret = mnl_cb_run(buffer, ret, 0, nl_portid, NULL, NULL);
        if (ret == -1) {
            goto err;
        }
        if (ret == 0) {
            goto out;
        }
    }

err:
    perror(__func__);
out:
    if (set) {
        nftnl_set_free(set);
    }
}

static int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
              struct nfq_data *nfa, void *data)
/*
 * Userspace queue callback.
 */
{
    int packet_id = 0;
    struct nfqnl_msg_packet_hdr* ph = nfq_get_msg_packet_hdr(nfa);
    if (ph) {
        packet_id = ntohl(ph->packet_id);
    }
    unsigned char* payload;
    int payload_len = nfq_get_payload(nfa, &payload);
    if (payload_len >= (int) sizeof(struct iphdr)) {
        struct iphdr* hdr = (struct iphdr*) payload;
        printf("saddr=%x daddr=%x\n", hdr->saddr, hdr->daddr);
        // classify packet
        uint32_t a = ntohl(hdr->daddr) >> 8;
        char* set_name = (bitmap[a >> 3] & (1 << (a & 7)))? set_hit : set_miss;
        add_to_set(hdr->daddr, set_name);
    }
    return nfq_set_verdict(qh, packet_id, NF_ACCEPT, 0, NULL);
}

int main(int argc, char *argv[])
{
    if (argc < 6 || argc > 7) {
        char* progname = strrchr(argv[0], '/');
        if (progname) {
            progname++;
        } else {
            progname = argv[0];
        }
        fprintf(
            stderr,
            "This program classifies IPv4 destination addresses using bitmap\n\
             and appends address to the <set-hit> or <set-miss>\n\
             \n\
             Usage: %s <map-file> <family> <table> <set-hit> <set-miss> [<queue-number>]\n",
             progname
        );
        return 1;
    }

    char* map_filename = argv[1];

    if (strcmp(argv[2], "ip") == 0)
        family = NFPROTO_IPV4;
    else if (strcmp(argv[2], "ip6") == 0)
        family = NFPROTO_IPV6;
    else if (strcmp(argv[2], "inet") == 0)
        family = NFPROTO_INET;
    else if (strcmp(argv[2], "bridge") == 0)
        family = NFPROTO_BRIDGE;
    else if (strcmp(argv[2], "arp") == 0)
        family = NFPROTO_ARP;
    else {
        fprintf(stderr, "Unknown family %s. Should be one of: ip, ip6, inet, bridge, arp\n", argv[2]);
        return 1;
    }

    table    = argv[3];
    set_hit  = argv[4];
    set_miss = argv[5];

    queue = 0;
    if (argc == 7) {
        int i = atoi(argv[6]);
        if (queue < 0 || queue > 65535) {
            fprintf(stderr, "Bad queue number %u, should be in range 0-65535\n", queue);
            return 1;
        }
        queue = (uint32_t) i;
    }

    // load map
    int fd_map = open(map_filename, O_RDONLY);
    if (fd_map == -1) {
        perror(map_filename);
        return 1;
    }
    ssize_t bytes_read = read(fd_map, bitmap, sizeof(bitmap));
    if (bytes_read == -1) {
        perror(map_filename);
        close(fd_map);
        return 1;
    }
    close(fd_map);

    if (bytes_read != sizeof(bitmap)) {
        fputs("Cannot read bitmap file\n", stderr);
        return 1;
    }

    // open netlnk socket
    nl_sock = mnl_socket_open(NETLINK_NETFILTER);
    if (!nl_sock) {
        perror("mnl_socket_open");
        return 1;
    }
    if (mnl_socket_bind(nl_sock, 0, MNL_SOCKET_AUTOPID) < 0) {
        perror("mnl_socket_bind");
        return 1;
    }
    nl_portid = mnl_socket_get_portid(nl_sock);

    // open queue handler
    struct nfq_handle *h = nfq_open();
    if (!h) {
        fprintf(stderr, "error during nfq_open()\n");
        return 1;
    }

    // unbind existing nf_queue handler for AF_INET, if any
    if (nfq_unbind_pf(h, AF_INET) < 0) {
        fprintf(stderr, "error during nfq_unbind_pf()\n");
        return 1;
    }

    // bind nfnetlink_queue as nf_queue handler for AF_INET
    if (nfq_bind_pf(h, AF_INET) < 0) {
        fprintf(stderr, "error during nfq_bind_pf()\n");
        return 1;
    }

    struct nfq_q_handle *qh = nfq_create_queue(h, queue, &cb, NULL);
    if (!qh) {
        fprintf(stderr, "error during nfq_create_queue()\n");
        return 1;
    }

    // set copy mode -- copy only IP headers of packet
    if (nfq_set_mode(qh, NFQNL_COPY_PACKET, sizeof(struct iphdr)) < 0) {
        fprintf(stderr, "can't set packet_copy mode\n");
        return 1;
    }

    // set NFQA_CFG_F_GSO flag as recommended
    if (nfq_set_queue_flags(qh, NFQA_CFG_F_GSO, NFQA_CFG_F_GSO)) {
        fprintf(stderr, "Failed setting NFQA_CFG_F_GSO flag.\n");
    }

    int fd = nfq_fd(h);
    for (;;) {
        char buf[4096] __attribute__ ((aligned));
        int len = recv(fd, buf, sizeof(buf), 0);
        if (len >= 0) {
            nfq_handle_packet(h, buf, len);
            continue;
        }
        if (len < 0 && errno == ENOBUFS) {
            fputs("Losing packets!\n", stderr);
            continue;
        }
        perror(nullptr);
        break;
    }
    nfq_destroy_queue(qh);
    nfq_close(h);
    mnl_socket_close(nl_sock);
    return 0;
}
