/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Joseph Eagar.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/bmesh/operators/bmo_beautify.c
 *  \ingroup bmesh
 */

#include "BLI_math.h"

#include "MEM_guardedalloc.h"

#include "bmesh.h"
#include "intern/bmesh_operators_private.h"

// #define DEBUG_TIME

#ifdef DEBUG_TIME
#  include "PIL_time.h"
#endif

/* -------------------------------------------------------------------- */
/* GHash for edge rotation */

typedef struct EdRotState {
	int v1, v2; /*	edge vert, small -> large */
	int f1, f2; /*	face vert, small -> large */
} EdRotState;

static unsigned int erot_ghashutil_hash(const void *ptr)
{
	const EdRotState *e_state = (const EdRotState *)ptr;
	unsigned int
	hash  = BLI_ghashutil_inthash(SET_INT_IN_POINTER(e_state->v1));
	hash ^= BLI_ghashutil_inthash(SET_INT_IN_POINTER(e_state->v2));
	hash ^= BLI_ghashutil_inthash(SET_INT_IN_POINTER(e_state->f1));
	hash ^= BLI_ghashutil_inthash(SET_INT_IN_POINTER(e_state->f2));
	return hash;
}
static int erot_ghashutil_cmp(const void *a, const void *b)
{
	const EdRotState *e_state_a = (const EdRotState *)a;
	const EdRotState *e_state_b = (const EdRotState *)b;
	if      (e_state_a->v1 < e_state_b->v1) return -1;
	else if (e_state_a->v1 > e_state_b->v1) return  1;
	else if (e_state_a->v2 < e_state_b->v2) return -1;
	else if (e_state_a->v2 > e_state_b->v2) return  1;
	else if (e_state_a->f1 < e_state_b->f1) return -1;
	else if (e_state_a->f1 > e_state_b->f1) return  1;
	else if (e_state_a->f2 < e_state_b->f2) return -1;
	else if (e_state_a->f2 > e_state_b->f2) return  1;
	else                                    return  0;
}

static GHash *erot_ghash_new(void)
{
	return BLI_ghash_new(erot_ghashutil_hash, erot_ghashutil_cmp, __func__);
}

/* ensure v0 is smaller */
#define EDGE_ORD(v0, v1) \
	if (v0 > v1) {       \
		v0 ^= v1;        \
		v1 ^= v0;        \
		v0 ^= v1;        \
	} (void)0

static void erot_state_ex(const BMEdge *e, int v_index[2], int f_index[2])
{
	BLI_assert(BM_edge_is_manifold((BMEdge *)e));
	BLI_assert(BM_vert_in_edge(e, e->l->prev->v)              == false);
	BLI_assert(BM_vert_in_edge(e, e->l->radial_next->prev->v) == false);

	/* verts of the edge */
	v_index[0] = BM_elem_index_get(e->v1);
	v_index[1] = BM_elem_index_get(e->v2);
	EDGE_ORD(v_index[0], v_index[1]);

	/* verts of each of the 2 faces attached to this edge
	 * (that are not apart of this edge) */
	f_index[0] = BM_elem_index_get(e->l->prev->v);
	f_index[1] = BM_elem_index_get(e->l->radial_next->prev->v);
	EDGE_ORD(f_index[0], f_index[1]);
}

static void erot_state_current(const BMEdge *e, EdRotState *e_state)
{
	erot_state_ex(e, &e_state->v1, &e_state->f1);
}

static void erot_state_alternate(const BMEdge *e, EdRotState *e_state)
{
	erot_state_ex(e, &e_state->f1, &e_state->v1);
}

/* -------------------------------------------------------------------- */
/* Util for setting edge tag once rotated */

/* we have rotated an edge, tag other egdes and clear this one */
static void bm_edge_tag_rotated(BMEdge *e)
{
	BMLoop *l;
	BLI_assert(e->l->f->len == 3 &&
	           e->l->radial_next->f->len == 3);

	l = e->l;
	BM_elem_flag_enable(l->next->e, BM_ELEM_TAG);
	BM_elem_flag_enable(l->prev->e, BM_ELEM_TAG);
	l = l->radial_next;
	BM_elem_flag_enable(l->next->e, BM_ELEM_TAG);
	BM_elem_flag_enable(l->prev->e, BM_ELEM_TAG);
}

/* -------------------------------------------------------------------- */
/* Beautify Fill */

#define ELE_NEW		1
#define FACE_MARK	2

/**
 * \note All edges in \a edge_array must be tagged and
 * have their index values set according to their position in the array.
 */
static void bm_mesh_beautify_fill(BMesh *bm, BMEdge **edge_array, const int edge_array_len)
{
	GHash      **edge_state_arr  = MEM_callocN(edge_array_len * sizeof(GHash *), __func__);
	BLI_mempool *edge_state_pool = BLI_mempool_create(sizeof(EdRotState), 512, 512, BLI_MEMPOOL_SYSMALLOC);
	bool is_breaked;
	int i;

#ifdef DEBUG_TIME
	TIMEIT_START(beautify_fill);
#endif

	do {
		is_breaked = true;

		for (i = 0; i < edge_array_len; i++) {
			BMEdge *e = edge_array[i];
			GHash *e_state_hash;

			float v1_xy[2], v2_xy[2], v3_xy[2], v4_xy[2];

			BLI_assert(BM_edge_is_manifold(e) == true);
			BLI_assert(BMO_elem_flag_test(bm, e->l->f, FACE_MARK) &&
			           BMO_elem_flag_test(bm, e->l->radial_next->f, FACE_MARK));

			if (!BM_elem_flag_test(e, BM_ELEM_TAG)) {
				continue;
			}
			else {
				/* don't check this edge again, unless adjaced edges are rotated */
				BM_elem_flag_disable(e, BM_ELEM_TAG);
			}

			/* check we're not moving back into a state we have been in before */
			e_state_hash = edge_state_arr[i];
			if (e_state_hash != NULL) {
				EdRotState e_state_alt;
				erot_state_alternate(e, &e_state_alt);
				if (BLI_ghash_haskey(e_state_hash, (void *)&e_state_alt)) {
					// printf("  skipping, we already have this state\n");
					continue;
				}
			}

			{
				const float *v1, *v2, *v3, *v4;
				bool is_zero_a, is_zero_b;
				float no[3];
				float axis_mat[3][3];

				v1 = e->l->prev->v->co;               /* first face co */
				v2 = e->l->v->co;                     /* e->v1 or e->v2*/
				v3 = e->l->radial_next->prev->v->co;  /* second face co */
				v4 = e->l->next->v->co;               /* e->v1 or e->v2*/

				if (UNLIKELY(v1 == v3)) {
					// printf("This should never happen, but does sometimes!\n");
					continue;
				}

				// printf("%p %p %p %p - %p %p\n", v1, v2, v3, v4, e->l->f, e->l->radial_next->f);
				BLI_assert((ELEM3(v1, v2, v3, v4) == false) &&
				           (ELEM3(v2, v1, v3, v4) == false) &&
				           (ELEM3(v3, v1, v2, v4) == false) &&
				           (ELEM3(v4, v1, v2, v3) == false));

				is_zero_a = area_tri_v3(v2, v3, v4) <= FLT_EPSILON;
				is_zero_b = area_tri_v3(v2, v4, v1) <= FLT_EPSILON;

				if (LIKELY(is_zero_a == false && is_zero_b == false)) {
					float no_a[3], no_b[3];
					normal_tri_v3(no_a, v2, v3, v4);  /* a */
					normal_tri_v3(no_b, v2, v4, v1);  /* b */
					add_v3_v3v3(no, no_a, no_b);
					if (UNLIKELY(normalize_v3(no) <= FLT_EPSILON)) {
						continue;
					}
				}
				else if (is_zero_a == false) {
					normal_tri_v3(no, v2, v3, v4);  /* a */
				}
				else if (is_zero_b == false) {
					normal_tri_v3(no, v2, v4, v1);  /* b */
				}
				else {
					/* both zero area, no useful normal can be calculated */
					continue;
				}

				// { float a = angle_normalized_v3v3(no_a, no_b); printf("~ %.7f\n", a); fflush(stdout);}

				axis_dominant_v3_to_m3(axis_mat, no);
				mul_v2_m3v3(v1_xy, axis_mat, v1);
				mul_v2_m3v3(v2_xy, axis_mat, v2);
				mul_v2_m3v3(v3_xy, axis_mat, v3);
				mul_v2_m3v3(v4_xy, axis_mat, v4);
			}

			// printf("%p %p %p %p - %p %p\n", v1, v2, v3, v4, e->l->f, e->l->radial_next->f);

			if (is_quad_convex_v2(v1_xy, v2_xy, v3_xy, v4_xy)) {
				float len1, len2, len3, len4, len5, len6, opp1, opp2, fac1, fac2;
				/* testing rule:
				 * the area divided by the total edge lengths
				 */
				len1 = len_v2v2(v1_xy, v2_xy);
				len2 = len_v2v2(v2_xy, v3_xy);
				len3 = len_v2v2(v3_xy, v4_xy);
				len4 = len_v2v2(v4_xy, v1_xy);
				len5 = len_v2v2(v1_xy, v3_xy);
				len6 = len_v2v2(v2_xy, v4_xy);

				opp1 = area_tri_v2(v1_xy, v2_xy, v3_xy);
				opp2 = area_tri_v2(v1_xy, v3_xy, v4_xy);

				fac1 = opp1 / (len1 + len2 + len5) + opp2 / (len3 + len4 + len5);

				opp1 = area_tri_v2(v2_xy, v3_xy, v4_xy);
				opp2 = area_tri_v2(v2_xy, v4_xy, v1_xy);

				fac2 = opp1 / (len2 + len3 + len6) + opp2 / (len4 + len1 + len6);

				if (fac1 > fac2) {
					e = BM_edge_rotate(bm, e, false, BM_EDGEROT_CHECK_EXISTS);
					if (LIKELY(e)) {

						/* add the new state into the hash so we don't move into this state again
						 * note: we could add the previous state too but this isn't essential)
						 *       for avoiding eternal loops */
						EdRotState *e_state = BLI_mempool_alloc(edge_state_pool);
						erot_state_current(e, e_state);
						if (UNLIKELY(e_state_hash == NULL)) {
							edge_state_arr[i] = e_state_hash = erot_ghash_new();  /* store previous state */
						}
						BLI_assert(BLI_ghash_haskey(e_state_hash, (void *)e_state) == false);
						BLI_ghash_insert(e_state_hash, e_state, NULL);


						// printf("  %d -> %d, %d\n", i, BM_elem_index_get(e->v1), BM_elem_index_get(e->v2));

						/* maintain the index array */
						edge_array[i] = e;
						BM_elem_index_set(e, i);

						/* tag other edges so we know to check them again */
						bm_edge_tag_rotated(e);

						/* update flags */
						BMO_elem_flag_enable(bm, e, ELE_NEW);
						BMO_elem_flag_enable(bm, e->l->f, FACE_MARK | ELE_NEW);
						BMO_elem_flag_enable(bm, e->l->radial_next->f, FACE_MARK | ELE_NEW);
						is_breaked = false;
					}
				}
			}
		}
	} while (is_breaked == false);

	for (i = 0; i < edge_array_len; i++) {
		if (edge_state_arr[i]) {
			BLI_ghash_free(edge_state_arr[i], NULL, NULL);
		}
	}

	MEM_freeN(edge_state_arr);
	BLI_mempool_destroy(edge_state_pool);

#ifdef DEBUG_TIME
	TIMEIT_END(beautify_fill);
#endif
}


void bmo_beautify_fill_exec(BMesh *bm, BMOperator *op)
{
	BMOIter siter;
	BMFace *f;
	BMEdge *e;

	BMEdge **edge_array;
	int edge_array_len = 0;

	BMO_ITER (f, &siter, op->slots_in, "faces", BM_FACE) {
		if (f->len == 3) {
			BMO_elem_flag_enable(bm, f, FACE_MARK);
		}
	}

	/* will over alloc if some edges can't be rotated */
	edge_array = MEM_mallocN(sizeof(*edge_array) *  BMO_slot_buffer_count(op->slots_in, "edges"), __func__);

	BMO_ITER (e, &siter, op->slots_in, "edges", BM_EDGE) {

		/* edge is manifold and can be rotated */
		if (BM_edge_rotate_check(e) &&
			/* faces are tagged */
			BMO_elem_flag_test(bm, e->l->f, FACE_MARK) &&
			BMO_elem_flag_test(bm, e->l->radial_next->f, FACE_MARK))
		{
			BM_elem_index_set(e, edge_array_len);  /* set_dirty */
			BM_elem_flag_enable(e, BM_ELEM_TAG);
			edge_array[edge_array_len] = e;
			edge_array_len++;
		}
	}
	bm->elem_index_dirty |= BM_EDGE;

	bm_mesh_beautify_fill(bm, edge_array, edge_array_len);

	MEM_freeN(edge_array);

	BMO_slot_buffer_from_enabled_flag(bm, op, op->slots_out, "geom.out", BM_EDGE | BM_FACE, ELE_NEW);
}
