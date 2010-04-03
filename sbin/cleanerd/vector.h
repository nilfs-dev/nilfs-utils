/*
 * vector.h - resizable array.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 *
 * Maintained by Ryusuke Konishi <ryusuke@osrg.net> from 2008.
 */

#ifndef VECTOR_H
#define VECTOR_H

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

static inline void nilfs_vector_sort(struct nilfs_vector *vector,
				     int (*compar)(const void *, const void *))
{
	qsort(vector->v_data, vector->v_nelems, vector->v_elemsize, compar);
}

#endif	/* VECTOR_H */
