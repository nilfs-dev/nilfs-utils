/*
 * vector.c - resizable array (should be replaced with STL vector).
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#include <errno.h>
#include "vector.h"


/**
 * nilfs_vector_create - create a vector
 * @elemsize: element size
 *
 * Description: nilfs_vector_create() creates a new vector, which is a
 * resizable array of data. The size of each element is specified by
 * @elemsize.
 *
 * Return Value: On success, the pointer to the newly-created vector is
 * returned. On error, NULL is returned.
 */
struct nilfs_vector *nilfs_vector_create(size_t elemsize)
{
	struct nilfs_vector *vector;

	if (elemsize == 0) {
		errno = EINVAL;
		return NULL;
	}

	vector = malloc(sizeof(struct nilfs_vector));
	if (!vector)
		return NULL;

	vector->v_data = malloc(elemsize * NILFS_VECTOR_INIT_MAXELEMS);
	if (!vector->v_data) {
		free(vector);
		return NULL;
	}

	vector->v_elemsize = elemsize;
	vector->v_maxelems = NILFS_VECTOR_INIT_MAXELEMS;
	vector->v_nelems = 0;

	return vector;
}

/**
 * nilfs_vector_destroy - destroy a vector
 * @vector: vector
 *
 * Description: nilfs_vector_destroy() destroys the vector specified by
 * @vector, which must have been returned by a previous call to
 * nilfs_vector_create().
 */
void nilfs_vector_destroy(struct nilfs_vector *vector)
{
	if (vector != NULL) {
		free(vector->v_data);
		free(vector);
	}
}

/**
 * nilfs_vector_get_new_element - add a new element
 * @vector: vector
 *
 * Description: nilfs_vector_get_new_element() adds a new element at the end
 * of the vector @vector.
 *
 * Return Value: on success, the pointer to the new element is returned. On
 * error, NULL is returned.
 */
void *nilfs_vector_get_new_element(struct nilfs_vector *vector)
{
	void *data;
	size_t maxelems;

	/* resize array if necessary */
	if (vector->v_nelems >= vector->v_maxelems) {
		maxelems = vector->v_maxelems * NILFS_VECTOR_FACTOR;
		data = realloc(vector->v_data, vector->v_elemsize * maxelems);
		if (!data)
			return NULL;
		vector->v_data = data;
		vector->v_maxelems = maxelems;
	}
	return vector->v_data + vector->v_elemsize * vector->v_nelems++;
}

/**
 * nilfs_vector_delete_elements - delete elements
 * @vector: vector
 * @index: index
 * @nelems: number of elements to be deleted
 *
 * Description: nilfs_vector_delete_elements() deletes @nelems elements from
 * @index.
 *
 * Return Value: On success, 0 is returned. On error, -1 is returned.
 */
int nilfs_vector_delete_elements(struct nilfs_vector *vector,
				 unsigned int index,
				 size_t nelems)
{
	if ((index >= vector->v_nelems) ||
	    (index + nelems - 1 >= vector->v_nelems)) {
		errno = EINVAL;
		return -1;
	}

	if (index + nelems - 1 < vector->v_nelems - 1)
		memmove(vector->v_data + vector->v_elemsize * index,
			vector->v_data + vector->v_elemsize * (index + nelems),
			(vector->v_nelems - (index + nelems)) *
			vector->v_elemsize);
	vector->v_nelems -= nelems;
	return 0;
}
