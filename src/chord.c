#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "a4protos.h"
#include "chord_arg_parser.h"
#include "chord_functions.h"
#include "hash.h"
#include "chord.h"

int MAX_MSG_SIZE = 1024;  //?? idk what this should be

void printKey(uint64_t key) {
	printf("%" PRIu64, key);
}

// subtracts t1 from t0
void decrementTV(struct timeval *t0, struct timeval *t1) {
	if (t0->tv_usec < t1->tv_usec) {
		t0->tv_usec = 1 - (t1->tv_usec - t0->tv_usec);
		t0->tv_sec -= t1->tv_sec + 1;
	} else {
		t0->tv_sec -= t1->tv_sec;
		t0->tv_usec -= t1->tv_usec;
	}
}

int main(int argc, char *argv[]) {
	struct chord_arguments args;
	int sockfd;
	Node *my_node = malloc(sizeof(Node)), *succ = malloc(sizeof(Node)), *pred = malloc(sizeof(Node));

	// Confirm correct number of args
	if (argc < 11 || argc > 17) {
		perror("Wrong number of arguments\n");
		exit(-1);
	}

	// Parse Arguments
	args = chord_parseopt(argc, argv);

	sockfd = listenTCP(&args.my_address);
	// sockfd = bindUDP(args.my_address);

	Node sl[args.num_successors];
	Node finger_table[64]; 
	memset(sl, 0, args.num_successors * sizeof(Node));
	memset(finger_table, 0, 64 * sizeof(Node));

	// Hash
	struct sha1sum_ctx *ctx = sha1sum_create(NULL, 0);

	// Initialize our node, saving it's address, port, and key
	init_node(my_node, args.my_address, ctx);

	//  If neither --ja nor --jp is specified, then start a new ring by invoking create
	if (args.join_address.sin_port == 0 && args.join_address.sin_addr.s_addr == 0) {
		// create
		pred = NULL;
		succ = my_node;
		finger_table[0].address = my_node->address;
        finger_table[0].port = my_node->port;
        finger_table[0].key = my_node->key;
	} else {
		// join
		// init_node(my_node, args.join_address, ctx);   We initialize the node a few lines above, dont think thios is needed
		pred = NULL;
		succ = join(my_node, args.join_address, my_node->key);
		finger_table[0].address = succ->address;
        finger_table[0].port = succ->port;
        finger_table[0].key = succ->key;
	}

	// node_socks keeps track of previous connections (this assumes we use TCP) - I'm not entirely sure this is necessary
	fd_set fds;
	int max_nodes = 64, node_socks[64] = { 0 }, i, sock, max_sock;

	// initialize the timevals for the periodic functions
	struct timeval sp_tv, ffp_tv, cpp_tv;
	sp_tv.tv_sec = args.stablize_period;
	sp_tv.tv_usec = 0;
	ffp_tv.tv_sec = args.fix_fingers_period;
	ffp_tv.tv_usec = 0;
	cpp_tv.tv_sec = args.check_predecessor_period;
	cpp_tv.tv_usec = 0;

	// print immediately
	fprintf(stdout, "> ");
	fflush(stdout);

	// select over stdin (for command input) and open sockets (for chord messages)
	for (;;) {
		// init the select fds
		FD_ZERO(&fds);
		FD_SET(sockfd, &fds);        // og sock
		FD_SET(STDIN_FILENO, &fds);  // stdin
		max_sock = sockfd;

		for (i = 0; i < max_nodes; i++) {
			sock = node_socks[i];
			if (sock > 0) {
				FD_SET(sock, &fds);
			}
			if (sock > max_sock) {
				max_sock = sock;
			}
		}

		// determine which function wait period is shortest and assign it to tv for select
		struct timeval tv;
		int call;  // which periodic function to call: 0-stabilize, 1-fixFingers, 2-checkPred
		double least, sp, ffp, cpp;
		sp = sp_tv.tv_usec / 1000000 + sp_tv.tv_sec;
		ffp = ffp_tv.tv_usec / 1000000 + ffp_tv.tv_sec;
		cpp = cpp_tv.tv_usec / 1000000 + cpp_tv.tv_sec;
		least = sp;
		call = 0;
		tv.tv_sec = sp_tv.tv_sec;
		tv.tv_usec = sp_tv.tv_usec;
		if (ffp < least) {
			least = ffp;
			call = 1;
			tv.tv_sec = ffp_tv.tv_sec;
			tv.tv_usec = ffp_tv.tv_usec;
		}
		if (cpp < least) {
			least = cpp;
			call = 2;
			tv.tv_sec = cpp_tv.tv_sec;
			tv.tv_usec = cpp_tv.tv_usec;
		}

		// do the select
		int r = select(max_sock + 1, &fds, NULL, NULL, &tv);
		if (r < 0) {
			perror("select failed");
		} else if (r == 0) {
			// timeout
			if (call == 0) {
				// call stabilize and update the timevals accordingly
				stabilize(sl, args.num_successors, succ, my_node);

				decrementTV(&ffp_tv, &sp_tv);
				decrementTV(&cpp_tv, &sp_tv);

				sp_tv.tv_sec = args.stablize_period;
				sp_tv.tv_usec = 0;
			} else if (call == 1) {
				// call fix fingers and update timevals
				fix_fingers(my_node, succ, pred, finger_table);

				decrementTV(&sp_tv, &ffp_tv);
				decrementTV(&cpp_tv, &ffp_tv);

				ffp_tv.tv_sec = args.fix_fingers_period;
				ffp_tv.tv_usec = 0;
			} else if (call == 2) {
				// call check pred and update timevals
				check_predecessor(pred);

				decrementTV(&sp_tv, &cpp_tv);
				decrementTV(&ffp_tv, &cpp_tv);

				cpp_tv.tv_sec = args.check_predecessor_period;
				cpp_tv.tv_usec = 0;
			}
		} else {
			if (FD_ISSET(STDIN_FILENO, &fds)) {
				// command from stdin
				char buf[1024];

				fgets(buf, 1024, stdin);
				char *cmd = malloc(sizeof(char) * 11);
				char *str = malloc(sizeof(char) * 1024);

				// str needs to be able to accept strings with white spaces
				size_t r = sscanf(buf, "%10s %[^\n]s", cmd, str);
				if (r <= 0) {
					perror("reading input");
				} else if (r == 1) {
					// PrintState or quit
					if (strcmp("PrintState", cmd) == 0) {
						// PrintState
						print_state(my_node, finger_table, sl, args.num_successors);
					} else if (strcmp("quit", cmd) == 0) {
						exit(0);
					} else {
						// invalid cmd
						printf("Invalid command. Use: \"Lookup <string>\", \"PrintState\", or \"quit\"\n");
					}

				} else if (strcmp("Lookup", cmd) == 0) {
					// lookup
					lookup(str, ctx, my_node, succ, pred, finger_table);
				} else {
					// invalid
					printf("Invalid command. Use: \"Lookup <string>\", \"PrintState\", or \"quit\"");
				}

				// print immediately
				fprintf(stdout, "> ");
				fflush(stdout);

				free(cmd);
				free(str);

			} else if (FD_ISSET(sockfd, &fds)) {
				// new chord connection; accept it and put it in the first open slot in node_socks
				if ((sock = accept(sockfd, NULL, NULL)) < 0) {
					perror("accept machine broke");
					exit(-1);
				}

				for (i = 0; i < max_nodes; i++) {
					if (node_socks[i] == 0) {
						node_socks[i] = sock;
						break;
					}
				}
			} else {
				// old chord connection
				for (i = 0; i < max_nodes; i++) {
					sock = node_socks[i];
					if (FD_ISSET(sock, &fds)) {
						ChordMessage *msg = recv_message(sock);
						
						if (msg == NULL) {
							// connection reset
							close(sock);
							node_socks[i] = 0;
						} else {
							// Init message to return						
							ChordMessage ret_msg = CHORD_MESSAGE__INIT;

							if (msg->has_query_id) {
								// TODO: ??
							}
							switch (msg->msg_case)
							{
							case CHORD_MESSAGE__MSG_FIND_SUCCESSOR_REQUEST: //deprecated for r_find_succ_req, should never recv this
							{
								fprintf(stdout, "recv'd wrong find succ req type\n");
								// find succ
								// uint64_t key_to_find = msg->find_successor_request->key;
								// Which node do we pick?
								// Node *succ = find_successor(my_node, args.join_address, key_to_find);


								break;
							}
							case CHORD_MESSAGE__MSG_CHECK_PREDECESSOR_REQUEST:
							{
								// check pred
								ret_msg.msg_case = CHORD_MESSAGE__MSG_CHECK_PREDECESSOR_RESPONSE;
								CheckPredecessorResponse resp = CHECK_PREDECESSOR_RESPONSE__INIT;
								ret_msg.check_predecessor_response = &resp;

								send_message(sock, &ret_msg);

								break;
							}
							case CHORD_MESSAGE__MSG_GET_PREDECESSOR_REQUEST:
							{
								// get pred
								ret_msg.msg_case = CHORD_MESSAGE__MSG_GET_PREDECESSOR_RESPONSE;
								GetPredecessorResponse pred_resp = GET_PREDECESSOR_RESPONSE__INIT;
								Node node_to_send = NODE__INIT;
								copy_node(&node_to_send, pred); // WILL THIS SEGFAULT IF PRED == NULL ???
								pred_resp.node = &node_to_send;
								ret_msg.get_predecessor_response = &pred_resp;

								send_message(sock, &ret_msg);

								break;
							}
							case CHORD_MESSAGE__MSG_NOTIFY_REQUEST:
							{
								// notify
								notify(my_node, msg->notify_request->node, pred);

								// sending a response but not sure if necessary
								ret_msg.msg_case = CHORD_MESSAGE__MSG_NOTIFY_RESPONSE;
								NotifyResponse notify_resp = NOTIFY_RESPONSE__INIT;
								ret_msg.notify_response = &notify_resp;

								send_message(sock, &ret_msg);

								break;
							}
							case CHORD_MESSAGE__MSG_GET_SUCCESSOR_LIST_REQUEST:
							{
								// Get succ list
								ret_msg.msg_case = CHORD_MESSAGE__MSG_GET_SUCCESSOR_LIST_RESPONSE;

								GetSuccessorListResponse sl_resp = GET_SUCCESSOR_LIST_RESPONSE__INIT;

								sl_resp.n_successors = args.num_successors;
								
								// This will probably SEGFAULT as the nodes are not initialized before being packed!!!
								sl_resp.successors = malloc(sizeof(Node) * args.num_successors);
								for (int i = 0; i < args.num_successors; i++) {
									sl_resp.successors[i]->address = sl[i].address;
									sl_resp.successors[i]->port = sl[i].port;
									sl_resp.successors[i]->key = sl[i].key;
								}

								ret_msg.get_successor_list_response = &sl_resp;

								send_message(sock, &ret_msg);

								free(sl_resp.successors);

								break;
							}
							case CHORD_MESSAGE__MSG_R_FIND_SUCC_REQ:
							{
								// find succ rec
								ret_msg.msg_case = CHORD_MESSAGE__MSG_R_FIND_SUCC_RESP;
								RFindSuccResp succ_resp = R_FIND_SUCC_RESP__INIT;
								Node node_to_send = NODE__INIT;
								Node *new_succ = find_successor(my_node, succ, pred, finger_table, msg->r_find_succ_req->key);
								
								copy_node(&node_to_send, new_succ);
								succ_resp.node = &node_to_send;
								succ_resp.key = msg->r_find_succ_req->key;

								ret_msg.r_find_succ_resp = &succ_resp;

								send_message(sock, &ret_msg);
								free(new_succ);

								break;
							}
							default:
								break;
							}

							chord_message__free_unpacked(msg, NULL);
						}

					}
				}
			}
		}
	}

	sha1sum_destroy(ctx);
	close(sockfd);

	free(my_node);
	free(succ);
	free(pred);

	return 0;
}
