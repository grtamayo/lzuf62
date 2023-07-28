/*
	Filename:   LZHASH2.C
	Author:     Gerald Tamayo
	Date:       May 17, 2008
	
	The code uses a hashing function to generate indices into
	a hash table of "doubly-linked" lists.

    *hashp added to record hash of position (i) and faster delete_lznode() calls. (2/4/2023)
*/
#include <stdio.h>
#include <stdlib.h>
#include "lzhash2.h"

/* stores the hashes of positions i */
int *hashp = NULL;

/* this is the *hash table* of listheads. */
int *lzhash = NULL;

/*
	these arrays contain the "previous" and "next" pointers
	of the virtual nodes.
*/
int *lzprev = NULL;
int *lznext = NULL;

/*
	allocate memory to the hash table and linked-list tables.
*/
int alloc_lzhash( int size )
{
	int i;
	
	lzhash = (int *) malloc( sizeof(int) * size );
	if ( !lzhash ) {
		fprintf(stderr, "\nError alloc: hash table.");
		return(0);
	}
	lzprev = (int *) malloc( sizeof(int) * size );
	if ( !lzprev ) {
		fprintf(stderr, "\nError alloc: prev table.");
		return(0);
	}
	lznext = (int *) malloc( sizeof(int) * size );
	if ( !lznext ) {
		fprintf(stderr, "\nError alloc: next table.");
		return(0);
	}
	hashp = (int *) malloc( sizeof(int) * size );
	if ( !hashp ) {
		fprintf(stderr, "\nError alloc: hashp table.");
		return(0);
	}
	/* initialize */
	for ( i = 0; i < size; i++ ){
		lzhash[i] = LZ_NULL;
		lznext[i] = LZ_NULL;
		lzprev[i] = LZ_NULL;
		hashp[i] = LZ_NULL;
	}
	return 1;
}

void free_lzhash( void )
{
	if ( lzhash ) free( lzhash );
	if ( lzprev ) free( lzprev );
	if ( lznext ) free( lznext );
	if ( hashp ) free( hashp );
}

/* ---- inserts a node (position i) into the hash list lzhash[h] ---- */
void insert_lznode( int h, int i )
{
	int k = lzhash[h];
	
	hashp[i] = h;  /* record this hash for position i. */
	
	/* always insert at the beginning. */
	lzhash[h] = i;
	lzprev[i] = LZ_NULL;
	lznext[i] = k;
	if ( k != LZ_NULL ) lzprev[k] = i;
	/* that's it! */
}

/* ---- deletes an LZ node (position i) ---- */
void delete_lznode( int h, int i )
{
	if ( lzhash[h] == i ) { /* the head of the list? */
		/* the next node becomes the head of the list */
		lzhash[h] = lznext[i];
		if ( lzhash[h] != LZ_NULL )  /* 4/25/2008 */
			lzprev[ lzhash[h] ] = LZ_NULL;
	}
	else {
		lznext[ lzprev[i] ] = lznext[i];
		/* only if there is a node following node i, shall we assign to it. */
		if ( lznext[i] != LZ_NULL ) lzprev[ lznext[i] ]= lzprev[i];
	}
}
