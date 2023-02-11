#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <endian.h>
#include <inttypes.h>

#include "a4protos.h"
#include "chord_functions.h"

// copies n1 to n0
void copy_node(Node *n0, Node *n1) {
    n0->address = n1->address;
    n0->port = n1->port;
    n0->key = n1->key;
}

uint64_t hash_addr(struct sockaddr_in addr, struct sha1sum_ctx *ctx) {
    // Convert ip and port to a string for hashing
    char *payload = addr_to_string(addr);
    uint8_t checksum[20];

    int error = sha1sum_finish(ctx, (const uint8_t *)payload, strlen(payload), checksum);
    if (error != 0) {
        perror("checksum finish error");
        exit(-1);
    }

    uint64_t ret = sha1sum_truncated_head(checksum);
    sha1sum_reset(ctx);
    free(payload);
    return ret;
}

void init_node(Node *node, struct sockaddr_in addr, struct sha1sum_ctx *ctx) {
    node__init(node);
    node->address = addr.sin_addr.s_addr;
    node->port = addr.sin_port;
    node->key = hash_addr(addr, ctx);
}

// send the ChordMessage
void send_message(int sock, const ChordMessage *msg) {
    // pack
    size_t len = chord_message__get_packed_size(msg);
    void *buf = malloc(len);
    chord_message__pack(msg, buf);

    // Send
    uint64_t length_to_send = htobe64((uint64_t)len);
    ssize_t retval = send(sock, &length_to_send, sizeof(uint64_t), 0);
    if (retval < 0) {
        free(buf);
        perror("send() failed\n");
        exit(-1);
    }
    assert(retval == sizeof(uint64_t));

    retval = send(sock, buf, len, 0);
    if (retval < 0) {
        free(buf);
        perror("send() failed\n");
        exit(-1);
    }
    assert((size_t)retval == len);

    free(buf);
}

// returned the recieved ChordMessage
ChordMessage *recv_message(int sock){
    // get succ
    uint64_t msg_len = 0;
    recvBytes(sock, sizeof(u_int64_t), &msg_len);

    msg_len = be64toh(msg_len);

    ChordMessage *resp;
    uint8_t *resp_buf = malloc(msg_len);

    // If we cannot recv return null
    ssize_t retval = recv(sock, resp_buf, msg_len, 0);
    if (retval <= 0) {
        perror("recv error");
        return NULL;
    } else if (retval == 0) {
        perror("recv() failed, connection closed prematurely\n");
        return NULL;
    }

    resp = chord_message__unpack(NULL, msg_len, resp_buf);
    if (resp == NULL) {
        perror("Failed to parse response\n");
        exit(-1);
    }

    free(resp_buf);

    return resp;
}

// essentially just the iterative find succ
Node *join(Node *n_prime, struct sockaddr_in n_prime_addr, uint64_t id) {
    // printf("join...\n");
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket() failed\n");
        exit(-1);
    }

    if (connect(sock, (struct sockaddr *)&n_prime_addr, sizeof(struct sockaddr_in)) < 0) {
        perror("connect() failed\n");
        exit(-1);
    }

    // Initialize request struct and set key
    ChordMessage msg = CHORD_MESSAGE__INIT;
    msg.msg_case = CHORD_MESSAGE__MSG_R_FIND_SUCC_REQ;

    RFindSuccReq req = R_FIND_SUCC_REQ__INIT;
    Node node_to_send = NODE__INIT;

    copy_node(&node_to_send, n_prime);
    req.requester = &node_to_send;
    req.key = id;    
    msg.r_find_succ_req = &req;

    // send
    send_message(sock, &msg);
    // recv
    ChordMessage *resp = recv_message(sock);

    Node *new_succ = malloc(sizeof(Node));
    copy_node(new_succ, resp->r_find_succ_resp->node);
    // uint64_t new_key = resp->r_find_succ_resp->key; //not sure what we would use this for if node already has their key
    chord_message__free_unpacked(resp, NULL);
    return new_succ;
}

Node *closest_preceding_node(Node *n, Node ft[], uint64_t id) {
    // printf("closest preceding...\n");
    // NEED TO CHECK successor list as well (p7 in paper)
    int i;
    Node *ret = malloc(sizeof(Node));
    for (i = 64; i > 1; i--) {
        if (ft[i].key > n->key && ft[i].key < id) {
            ret->address = ft[i].address;
            ret->port = ft[i].port;
            ret->key = ft[i].key;
            return ret;
        }
    }
    return n;
}

Node *find_successor(Node *n, Node *succ, Node *pred, Node ft[], uint64_t id) {
    // printf("find succ...\n");
    if (pred != NULL && id > pred->key && id <= n->key) {
        return n;

    } else if (succ != NULL && id > n->key && id <= succ->key) {
        return succ;

    } else {
        // forward query around the circle
        Node *next_node = closest_preceding_node(n, ft, id);

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0) {
            perror("socket() failed\n");
            exit(-1);
        }

        struct sockaddr_in addr;
        if (succ == n) {
            // there is only the one node in the chord so far
            return n;
        } else {
                       
            addr.sin_addr.s_addr = next_node->address;
            addr.sin_port = next_node->port;
        }
        addr.sin_family = AF_INET;

        if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
            perror("connect() failed\n");
            exit(-1);
        }

        free(next_node);

        // Initialize request struct and set key
        ChordMessage msg = CHORD_MESSAGE__INIT;
        msg.msg_case = CHORD_MESSAGE__MSG_R_FIND_SUCC_REQ;

        RFindSuccReq req = R_FIND_SUCC_REQ__INIT;
        Node node_to_send = NODE__INIT;

        copy_node(&node_to_send, n);
        req.requester = &node_to_send;
        req.key = id;

        msg.r_find_succ_req = &req;

        // send and recv
        send_message(sock, &msg);
        ChordMessage *resp = recv_message(sock);

        Node *new_succ = malloc(sizeof(Node));
        copy_node(new_succ, resp->r_find_succ_resp->node);
        // uint64_t new_key = resp->r_find_succ_resp->key; //not sure what we would use this for if node already has their key
        chord_message__free_unpacked(resp, NULL);
        return new_succ;
    }
}

void stabilize(Node sl[], int len, Node *successor, Node *my_node) {
    // printf("stabilize...\n");
    Node *x = malloc(sizeof(Node));

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket() failed\n");
        exit(-1);
    }

    // get first alive in succ list
    // NEED TO GET THE SUCCESSOR LIST FROM THE SUCCESSOR (P7 final paragraph of paper)
    for (int i = 0; i < len; i++) {
        struct sockaddr_in addr;
        addr.sin_port = sl[i].port;
        addr.sin_addr.s_addr = sl[i].address;
        addr.sin_family = AF_INET;

        if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
            // connection failed, try next successor unless its the last successor (then just return)
            if (i == len - 1) {
                return;
            }
            continue;
        }
        break;
    }

    // get succ's pred
    ChordMessage msg = CHORD_MESSAGE__INIT;
    msg.msg_case = CHORD_MESSAGE__MSG_GET_PREDECESSOR_REQUEST;
    GetPredecessorRequest pred_req = GET_PREDECESSOR_REQUEST__INIT;
    msg.get_predecessor_request = &pred_req;

    // send
    send_message(sock, &msg);
    ChordMessage *resp = recv_message(sock);

    // x is the pred
    copy_node(x, resp->get_predecessor_response->node);

    chord_message__free_unpacked(resp, NULL);

    // update succ if needed
    if (my_node->key < x->key && x->key <= successor->key) {
        copy_node(successor, x);
    }
    free(x);

    //notify        successor.notify(n)
    ChordMessage msg2 = CHORD_MESSAGE__INIT;
    msg2.msg_case = CHORD_MESSAGE__MSG_NOTIFY_REQUEST;
    NotifyRequest notify_req = NOTIFY_REQUEST__INIT;
    Node node_to_send = NODE__INIT;
    copy_node(&node_to_send, my_node);
    notify_req.node = &node_to_send;
    msg2.notify_request = &notify_req;

    send_message(sock, &msg2);
}

void fix_fingers(Node *node, Node *succ, Node *pred, Node ft[]) {
    // printf("fix fingers...\n");
    // reinitializes the finger table each time
    int i;
    Node *n = malloc(sizeof(Node));
    for (i = 0; i < 64; i++) {
        if (i > 64) {
            i = 1; //really do not know what this does
        }
        uint64_t key = node->key + (uint64_t)pow(2, i - 1); //does this need to be mod 64??
        n = find_successor(node, succ, pred, ft, key);
        ft[i].address = n->address;
        ft[i].port = n->port;
        ft[i].key = n->key;
    }

    free(n);
}

void check_predecessor(Node *pred) {
    // If pred is already NULL return
    if (pred == NULL) {
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed\n");
        exit(-1);
    }

    // construct pred_addr to connect with
    struct sockaddr_in pred_addr;
    pred_addr.sin_port = pred->port;
    pred_addr.sin_addr.s_addr = pred->address;
    pred_addr.sin_family = AF_INET;

    if (connect(sock, (struct sockaddr *)&pred_addr, sizeof(struct sockaddr_in)) < 0) {
        // connection failed, set pred to NULL and return
        pred = NULL;
        return;
    }

    // Build ChordMessage to send
    ChordMessage msg = CHORD_MESSAGE__INIT;
    msg.msg_case = CHORD_MESSAGE__MSG_CHECK_PREDECESSOR_REQUEST;
    CheckPredecessorRequest req = CHECK_PREDECESSOR_REQUEST__INIT;
    msg.check_predecessor_request = &req;

    // Send the check pred request
    send_message(sock, &msg);

    // Receive check pred response. Do nothing as we know the pred exists now
    ChordMessage *return_msg = recv_message(sock);

    chord_message__free_unpacked(return_msg, NULL);
    close(sock);
}

void notify(Node *n, Node *n_prime, Node *pred) {
    if (pred == NULL || (pred->key < n_prime->key && n_prime->key < n->key)) {
        pred->address = n_prime->address;
        pred->port = n_prime->port;
        pred->key = n_prime->key;
    }
}

void print_state(Node *n, Node ft[], Node sl[], int num_succ) {
    // print self
    struct in_addr addr;
    addr.s_addr = n->address;
    fprintf(stdout, "< Self %" PRIu64, n->key);
    fprintf(stdout, " %s %d\n", inet_ntoa(addr), n->port);

    // print succ list
    int i;
    for (i = 0; i < num_succ; i++) {
        addr.s_addr = sl[i].address;
        fprintf(stdout, "< Successor [%d] %" PRIu64, i + 1, sl[i].key);
        fprintf(stdout, " %s %d\n", inet_ntoa(addr), sl[i].port);
    }

    // print finger table
    for (i = 0; i < 64; i++) {
        addr.s_addr = ft[i].address;
        fprintf(stdout, "< Finger [%d] %" PRIu64, i + 1, ft[i].key);
        fprintf(stdout, " %s %d\n", inet_ntoa(addr), ft[i].port);
    }
}

void lookup(char *str, struct sha1sum_ctx *ctx, Node *n, Node *succ, Node *pred, Node ft[]) {
    uint64_t key;
    uint8_t checksum[20];

    int error = sha1sum_finish(ctx, (const uint8_t *)str, strlen(str), checksum);
    if (error != 0) {
        perror("checksum finish error");
        exit(-1);
    }

    key = sha1sum_truncated_head(checksum);
    sha1sum_reset(ctx);

    Node *node_succ = find_successor(n, succ, pred, ft, key);
    struct in_addr succ_addr;
    succ_addr.s_addr = node_succ->address;

    fprintf(stdout, "< %s ", str);
    fprintf(stdout, "%" PRIu64, key);
    fprintf(stdout, "\n< ");
    fprintf(stdout, "%" PRIu64, node_succ->key);
    fprintf(stdout, " %s %d\n", inet_ntoa(succ_addr), node_succ->port);
}