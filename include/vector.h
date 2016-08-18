/*
 * vector.h - resizable array
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */

#ifndef NILFS_VECTOR_H
#define NILFS_VECTOR_H

#include <stdlib.h>
#include <sys/types.h>

/**
 * struct nilfs_vector - a resizable array
 * @v_data: pointer to data
 * @v_elemsize: element size
 * @v_nelems: number of elements
 * @v_maxelems: maximum number of elements
 */
struct nilfs_vector {
	void *v_data;
	size_t v_elemsize;
	size_t v_nelems;
	size_t v_maxelems;
};

#define NILFS_VECTOR_INIT_MAXELEMS	256
#define NILFS_VECTOR_FACTOR		2


struct nilfs_vector *nilfs_vector_create(size_t);
void nilfs_vector_destroy(struct nilfs_vector *);
void *nilfs_vector_get_new_element(struct nilfs_vector *);
int nilfs_vector_delete_elements(struct nilfs_vector *, unsigned int, size_t);
void *nilfs_vector_insert_elements(struct nilfs_vector *vector,
				   unsigned int index, size_t nelems);
void nilfs_vector_clear(struct nilfs_vector *vector);

static inline void *nilfs_vector_get_data(const struct nilfs_vector *vector)
{
	return vector->v_data;
}

static inline size_t nilfs_vector_get_size(const struct nilfs_vector *vector)
{
	return vector->v_nelems;
}

static inline void *nilfs_vector_get_element(struct nilfs_vector *vector,
					     unsigned int index)
{
	return (index < vector->v_nelems) ?
		vector->v_data + vector->v_elemsize * index :
		NULL;
}

static inline int nilfs_vector_delete_element(struct nilfs_vector *vector,
					      unsigned int index)
{
	return nilfs_vector_delete_elements(vector, index, 1);
}

static inline void *nilfs_vector_insert_element(struct nilfs_vector *vector,
						unsigned int index)
{
	return nilfs_vector_insert_elements(vector, index, 1);
}

static inline void nilfs_vector_sort(struct nilfs_vector *vector,
				     int (*compar)(const void *, const void *))
{
	qsort(vector->v_data, vector->v_nelems, vector->v_elemsize, compar);
}

#endif	/* NILFS_VECTOR_H */
