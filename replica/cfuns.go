package replica

/*
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
//#include <bits/sigaction.h>

#include "../bft/libbyz/libbyz.h"
int Simple_size = 4096;
static void dump_profile() {
	// profil(0,0,0,0);
	// Byz_print_stats();
  Byz_print_stats2();
	exit(0);
}

// signal handler t dump profile information
void dump_handler() {
	struct sigaction act;
	act.sa_handler = dump_profile;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
}

// service function
int exec_command_cgo(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client, bool ro) {

  // A simple service.
  if (inb->contents[0] == 1) {
    // th_assert(inb->size == 8, "Invalid request");
    if (!(inb->size == 8)) {
      printf("Invalid request\n");
      exit(-1);
    }
    bzero(outb->contents, Simple_size);
    outb->size = Simple_size;
    return 0;
  }

  // th_assert((inb->contents[0] == 2 && inb->size == Simple_size) ||
      // (inb->contents[0] == 0 && inb->size == 8), "Invalid request");
  if (!((inb->contents[0] == 2 && inb->size == Simple_size) || (inb->contents[0] == 0 && inb->size == 8))) {
    printf("Invalid request\n");
    exit(-1);
  }
  *((long long*)(outb->contents)) = 0;
  outb->size = 8;
  return 0;
}

*/
import "C"
