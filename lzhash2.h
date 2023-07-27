/*
	Filename:   LZHASH2.H
	Author:     Gerald Tamayo
	Date:       May 17, 2008  (2/4/2023)
*/
#include <stdio.h>
#include <stdlib.h>

#if !defined(LZHASH_H)
	#define LZHASH_H

#define LZ_NULL  -1

extern int *hashp;
extern int *lzhash;
extern int *lzprev;
extern int *lznext;

/* ---- function prototypes. ---- */
int alloc_lzhash( int size );
void free_lzhash( void );
void insert_lznode( int h, int i );
void delete_lznode( int h, int i );

#endif
