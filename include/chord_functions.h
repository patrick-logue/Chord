#include "chord.pb-c.h"
#include "hash.h"

void init_node(Node *node, struct sockaddr_in addr, struct sha1sum_ctx *ctx);
void create(Node *node, Node *pred, Node *succ);
// returns the initial succ for joining nodes
Node *join(Node *n_prime, struct sockaddr_in n_prime_addr, uint64_t id);
void stabilize(Node sl[], int len, Node *successor, Node *my_node);
void notify(Node *n, Node *n_prime, Node *pred);
void fix_fingers(Node *node, Node *succ, Node *pred, Node ft[]);
void check_predecessor(Node *pred);
Node *get_successor_list();

// Recursive implementation of find_sucessor
Node *find_successor(Node *n, Node *succ, Node *pred, Node ft[], uint64_t id);
Node *closest_preceding_node(Node *n, Node ft[], uint64_t id);

void print_state(Node *n, Node ft[], Node sl[], int num_succ);
void lookup(char *str, struct sha1sum_ctx *ctx, Node *n, Node *succ, Node *pred, Node ft[]);

// copies n1 to n0
void copy_node(Node *n0, Node *n1);
void send_message(int sock, const ChordMessage *msg);
ChordMessage *recv_message(int sock);