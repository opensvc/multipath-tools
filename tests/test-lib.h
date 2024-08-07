#ifndef TEST_LIB_H_INCLUDED
#define TEST_LIB_H_INCLUDED

extern const int default_mask;
extern const char default_devnode[];
extern const char default_wwid[];
extern const char default_wwid_1[];

enum {
	BL_BY_DEVNODE	= (1 << 0),
	BL_BY_DEVICE	= (1 << 1),
	BL_BY_WWID	= (1 << 2),
	BL_BY_PROPERTY	= (1 << 3),
	BL_MASK = BL_BY_DEVNODE|BL_BY_DEVICE|BL_BY_WWID|BL_BY_PROPERTY,
	NEED_SELECT_PRIO = (1 << 8),
	NEED_FD		= (1 << 9),
	USE_VPD_VND	= (1 << 10),
	DEV_HIDDEN	= (1 << 11),
};

struct mocked_path {
	const char *vendor;
	const char *product;
	const char *rev;
	const char *wwid;
	const char *devnode;
	unsigned int flags;
};

struct mocked_path *fill_mocked_path(struct mocked_path *mp,
				     const char *vendor,
				     const char *product,
				     const char *rev,
				     const char *wwid,
				     const char *devnode,
				     unsigned int flags);

struct mocked_path *mocked_path_from_path(struct mocked_path *mp,
					  const struct path *pp);

void mock_pathinfo(int mask, const struct mocked_path *mp);
void mock_store_pathinfo(int mask, const struct mocked_path *mp);
struct path *mock_path__(vector pathvec,
			 const char *vnd, const char *prd,
			 const char *rev, const char *wwid,
			 const char *dev,
			 unsigned int flags, int mask);

#define mock_path(v, p) \
	mock_path__(hwt->vecs->pathvec,	(v), (p), "0", NULL, NULL,	\
		    0, default_mask)
#define mock_path_flags(v, p, f) \
	mock_path__(hwt->vecs->pathvec,	(v), (p), "0", NULL, NULL, \
		    (f), default_mask)
#define mock_path_blacklisted(v, p) \
	mock_path__(hwt->vecs->pathvec,	(v), (p), "0", NULL, NULL, \
		    BL_BY_DEVICE, default_mask)
#define mock_path_wwid(v, p, w) \
	mock_path__(hwt->vecs->pathvec,	(v), (p), "0", (w), NULL,	\
		    0, default_mask)
#define mock_path_wwid_flags(v, p, w, f) \
	mock_path__(hwt->vecs->pathvec,	(v), (p), "0", (w),		\
		    NULL, (f), default_mask)

struct multipath *mock_multipath__(struct vectors *vecs, struct path *pp);
#define mock_multipath(pp) mock_multipath__(hwt->vecs, (pp))

#endif
