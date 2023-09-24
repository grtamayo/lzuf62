/*
	Filename:	mtf.h
	Written by:	Gerald R. Tamayo, 2005/2023
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MTF_H
#define MTF_H

typedef struct mtf_list {
	int c;
	unsigned int f;
	struct mtf_list *prev;
	struct mtf_list *next;
} mtf_list_t;

extern int tSIZE;
extern mtf_list_t *p, *head, *table;

int alloc_mtf( int size );
void init_mtf( void );
void free_mtf_table( void );
static inline int mtf( int c );
static inline int get_mtf_c( int i );

#endif
