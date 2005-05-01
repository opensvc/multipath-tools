#define MINORBITS       20
#define MINORMASK       ((1U << MINORBITS) - 1)

#define MAJOR(dev)      ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)      ((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)    (((ma) << MINORBITS) | (mi))

#define print_dev_t(buffer, dev)                                        \
	sprintf((buffer), "%u:%u\n", MAJOR(dev), MINOR(dev))

#define format_dev_t(buffer, dev)                                       \
	({                                                              \
		sprintf(buffer, "%u:%u", MAJOR(dev), MINOR(dev));       \
		buffer;                                                 \
	})
