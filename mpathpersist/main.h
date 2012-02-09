static struct option long_options[] = {
	{"verbose", 1, 0, 'v'},
	{"clear", 0, 0, 'C'},
	{"device", 1, 0, 'd'},
	{"help", 0, 0, 'h'},
	{"hex", 0, 0, 'H'},
	{"in", 0, 0, 'i'},
	{"out", 0, 0, 'o'},
	{"param-aptpl", 0, 0, 'Z'},
	{"param-rk", 1, 0, 'K'},
	{"param-sark", 1, 0, 'S'},
	{"preempt", 0, 0, 'P'},
	{"preempt-abort", 0, 0, 'A'},
	{"prout-type", 1, 0, 'T'},
	{"read-full-status", 0, 0, 's'},
	{"read-keys", 0, 0, 'k'},
	{"read-reservation", 0, 0, 'r'},
	{"register", 0, 0, 'G'},
	{"register-ignore", 0, 0, 'I'},
	{"release", 0, 0, 'L'},
	{"report-capabilities", 0, 0, 'c'},		
	{"reserve", 0, 0, 'R'},
	{"transport-id", 1, 0, 'X'},
	{"alloc-length", 1, 0, 'l'},
	{0, 0, 0, 0}
};

static void usage(void);

