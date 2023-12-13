/*
	Filename:	mtf.c
	Written by:	Gerald R. Tamayo, 2005/2023
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mtf.h"

int tSIZE = 0;
mtf_list_t *p = NULL, *head = NULL, *table = NULL;

int alloc_mtf( int tsize )
{
	if ( tsize <= 0 ) tsize = 256;
	
	table=(mtf_list_t *) calloc(sizeof(mtf_list_t), tSIZE=tsize);
	if ( !table ) {
		fprintf(stderr, "error alloc!");
		return 0;
	}
	init_mtf();
	return 1;
}

void init_mtf(void)
{
	int i;
	
	/* initialize the list. */
	for ( i = tSIZE-1; i >= 0; i-- ) {
		table[i].c = i;
		table[i].f = i+tSIZE+1;
		table[i].next = &table[i-1];
		table[i].prev = &table[i+1];
	}
	table[tSIZE-1].prev = NULL;
	table[0].next = NULL;
	head = &table[tSIZE-1];
}

void free_mtf_table( void )
{
	if ( table ) free( table );
}

static inline int mtf( int c )
{
	int i = 0;
	
	/* find c. */
	p = head;
	while( p->c != c ) {
		i++;
		p = p->next;
	}
	/* move-to-front. */
	if ( p != head ) {
		if ( p->next ) {
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else p->prev->next = NULL;
		p->next = head;
		head->prev = p;
		head = p;
	} /* front, don't MTF! */
	
	return i;
}

static inline int get_mtf_c( int i )
{
	/* find c. */
	p = head;
	while( i-- ) {
		p = p->next;
	}
	/* move-to-front. */
	if ( p != head ) {
		if ( p->next ) {
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else p->prev->next = NULL;
		p->next = head;
		head->prev = p;
		head = p;
	}
	return p->c;
}
