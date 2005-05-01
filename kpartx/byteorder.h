#ifndef BYTEORDER_H_INCLUDED
#define BYTEORDER_H_INCLUDED

#if defined (__s390__) || defined (__s390x__)
#define le32_to_cpu(x)	( \
		(*(((unsigned char *) &(x)))) + \
		(*(((unsigned char *) &(x))+1) << 8) + \
		(*(((unsigned char *) &(x))+2) << 16) + \
		(*(((unsigned char *) &(x))+3) << 24) \
	)
#else
#define le32_to_cpu(x)	(x)
#endif

#endif				/* BYTEORDER_H_INCLUDED */
