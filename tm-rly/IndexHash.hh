/*
Timemachine
Copyright (c) 2006 Technische Universitaet Muenchen,
                   Technische Universitaet Berlin,
                   The Regents of the University of California
All rights reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the names of the copyright owners nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id$

#ifndef INDEXHASH_HH
#define INDEXHASH_HH

#include <assert.h>

#include "tm.h"
#include "IndexField.hh"
#include "IndexEntry.hh"
#include "Hash.hh"


/* The hash used for the Indexes. 
 *
 * We need several properties from the Index storage. First, 
 * we must be able to find entries quickly, sonce every packet 
 * will generate a lookup. So we need a hash. Furthermore we must
 * be able to write the date to disk in a sorted way. So we could
 * use a PQ. I tried this but it sucked. A std::priority_queu
 * uses too much CPU time (esp. for new/delete) and too much
 * space (node overhead, etc.). Another problem is, that writing
 * the index to disk is time critical! Packet loss mostly happens
 * while the index is written to disk. This means, the reading
 * from the PQ must be as fast as possible
 *
 * So, the solution to this was to use an AVL search tree and
 * a hash. To reduce the overhead for tree management, the 
 * tree pointers (left, right, parent) have been embedded in
 * the IndexEntry class. So we don't need any additional
 * memory for the tree. Another advantage is, that reading
 * the tree in sorted order only takes linear time. Fruthermore
 * while we traverse the tree we can only remove the associated
 * emelemt from the hash. Thus the actual write to disk will
 * only take linear time!
 */

class IndexHash {
	public:
		//typedef Hash<IndexField *, IndexEntry *> hash_t;
		typedef IndexEntry* hash_t;
		IndexHash(size_t size);
		~IndexHash();

		int clear();
		
		int getNumEntries() { return numEntries; };
		int getNumBuckets() { return numBuckets; };

		IndexEntry *lookup( IndexField* key); 


		void add(IndexField *key, IndexEntry *ie);
		void rebalance(IndexEntry *cur);

		void initWalk (void);

		// Do an in-order tree walk. Delete elements from the hash 
		// after they have been completely visted
		// if they have been c
		IndexEntry *getNextDelete (void); 

		int height, level;

	protected:
		hash_t *htable;
		unsigned numBuckets;
		unsigned numEntries;

		IndexEntry *troot; // root of the tree
		IndexEntry *tnext, *tprev, *tcur; // for tree traversal

		void eraseEntry(IndexEntry *ie);

		/* Rotations for the AVL tree */
#define connect_avl_nodes(p, dir, child) { (p)->dir=child; if(child) (child)->parent=p; }
		
		/* -, --, 0, +, ++ show the avl balance
		 *        y(--)              x(0)
		 *       / \                / \
		 *      /   \              /   \
		 *     x(-)  c   ==>      a     y (0)
		 *    / \                      / \
		 *   a   b                    b   c
		 *
		 */
		void rot_right(IndexEntry *y) {
			IndexEntry *x, *parent;
			parent = y->parent;
			x = y->left;
#ifdef TM_HEAVY_DEBUG
			assert(x->avlbal == -1);
			assert(y->avlbal == -2);
#endif

			connect_avl_nodes(y, left, x->right);
			connect_avl_nodes(x, right, y);
			
			if (parent) {
				if (y == parent->left)
					parent->left = x;
				else
					parent->right = x;
				x->parent = parent;
			}
			else  {
				troot = x;
				x->parent= NULL;
			}

			x->avlbal = y->avlbal = 0;
		}
		/* -, --, 0, +, ++ show the avl balance
		 *        y(--)              y(--)                     w(0)
		 *       / \                / \                       /   \
		 *      /   \              /   \                     /     \
		 *     x(+)  d   ==>     w(?)   d  ==>              x(?)    y(?)
		 *    / \               / \                         / \    / \
		 *   a   w(?)         x(?) c                       a   b  c   d
		 *      / \          / \
		 *     b   c        a   b
		 */
		void rot_left_right(IndexEntry *y) {
			IndexEntry *x, *parent, *w;
			parent = y->parent;
			x = y->left;
			w = x->right;
#ifdef TM_HEAVY_DEBUG
			assert(x->avlbal == 1);
			assert(y->avlbal == -2);
#endif
			
			// step1 
			connect_avl_nodes(x, right, w->left);
			connect_avl_nodes(w, left, x);

			//step2
			connect_avl_nodes(y, left, w->right);
			connect_avl_nodes(w, right, y);


			/*
			 Case 1: w has balance factor 0.
				This means that w is the new node. a, b, c, and d have height 0. After the rotations, x and y have balance factor 0.
			Case 2: w has balance factor -.
				a, b, and d have height h > 0, and c has height h - 1.
			Case 3: w has balance factor +.
				a, c, and d have height h > 0, and b has height h - 1.
			*/
			if (w->avlbal == -1) {
				x->avlbal = 0;
				y->avlbal = 1;
			}
			else if (w->avlbal == 0)
				x->avlbal = y->avlbal = 0;
			else {
				x->avlbal = -1;
				y->avlbal = 0;
			}
			w->avlbal = 0;
			
			if (parent) {
				if (y == parent->left)
					parent->left = w;
				else
					parent->right = w;
				w->parent = parent;
			}
			else {
				troot = w;
				w->parent = NULL;
			}
		}


		void rot_left(IndexEntry *y) {
			IndexEntry *x, *parent;
			parent = y->parent;
			x = y->right;
#ifdef TM_HEAVY_DEBUG
			assert(y->avlbal==2);
			assert(x->avlbal==1);
#endif

			connect_avl_nodes(y, right, x->left);
			connect_avl_nodes(x, left, y);
			
			if (parent) {
				if (y == parent->left)
					parent->left = x;
				else
					parent->right = x;
				x->parent = parent;
			}
			else {
				troot = x;
				x->parent = NULL;
			}

			x->avlbal = y->avlbal = 0;
		}

		void rot_right_left(IndexEntry *y) {
			IndexEntry *x, *parent, *w;
			parent = y->parent;
			x = y->right;
			w = x->left;
#ifdef TM_HEAVY_DEBUG
			assert(y->avlbal==2);
			assert(x->avlbal==-1);
#endif
			
			// step1 
			connect_avl_nodes(x, left, w->right);
			connect_avl_nodes(w, right, x);
			
			//step2
			connect_avl_nodes(y, right, w->left);
			connect_avl_nodes(w, left, y);

			/*
			 Case 1: w has balance factor 0.
				This means that w is the new node. a, b, c, and d have height 0. After the rotations, x and y have balance factor 0.
			Case 2: w has balance factor -.
				a, b, and d have height h > 0, and c has height h - 1.
			Case 3: w has balance factor +.
				a, c, and d have height h > 0, and b has height h - 1.
			*/
			if (w->avlbal == 1) {
				x->avlbal = 0;
				y->avlbal = -1;
			}
			else if (w->avlbal == 0)
				x->avlbal = y->avlbal = 0;
			else {
				x->avlbal = 1;
				y->avlbal = 0;
			}
			w->avlbal = 0;
			
			if (parent) {
				if (y == parent->left)
					parent->left = w;
				else
					parent->right = w;
				w->parent = parent;
			}
			else {
				troot = w;
				w->parent = NULL;
			}
		}
};


#endif
