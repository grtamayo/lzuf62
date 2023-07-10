/*
	---- A Lempel-Ziv Unary (LZUF) Coding Implementation ----

	Filename:      lzuf621.c  (decompressor:  lzufd62/lzufd622)
	Written by:    Gerald Tamayo, Oct. 22, 2008 (2/24/2022)
	
	Version 2:
		(2/27/2022) Optional bit size for sliding window implemented, BITS = 12..20, default = 17.
		(7/07/2023) Optional bitsize of "hash bucket" search list BITS = 1..12, default = 9.");
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "utypes.h"
#include "gtbitio3.c"
#include "ucodes2.c"
#include "lzhash.c"
#include "mtf.c"

/* the decompressor's must also equal these values. */
#define LTCB              17              /* 12..20 tested working */
#ifdef LTCB 
    #define NUM_POS_BITS LTCB
#else 
    #define NUM_POS_BITS  15
#endif

#define MIN_LEN           4               /* minimum string size >= 2 */
#define MTF_SIZE        256
#define FAR_LIST_BITS     9
#define FAR_LIST        (1<<(FAR_LIST_BITS))

#define HASH_BYTES_N    4
/* 4-byte hash */
#define hash(buf,pos,mask1,mask2) \
	(((buf[ (pos)&(mask1)]<<hash_SHIFT) \
	^(buf[((pos)+1)&(mask1)]<<7) \
	^(buf[((pos)+2)&(mask1)]<<4) \
	^(buf[((pos)+3)&(mask1)]))&(mask2))
	
typedef struct {
	char algorithm[4];
	int64_t file_size;
	int num_pos_bits;
} file_stamp; 

typedef struct {
	unsigned int pos, len, ii;
} dpos_t;

unsigned int num_POS_BITS = NUM_POS_BITS; /* default */
unsigned int win_BUFSIZE  = 1<<NUM_POS_BITS;
unsigned int win_MASK;
unsigned int hash_SHIFT;
unsigned int pat_BUFSIZE;   /* must be a power of 2. */
unsigned int pat_MASK;
int far_LIST_BITS = FAR_LIST_BITS;  /* default */
int far_LIST = 1<<FAR_LIST_BITS;

dpos_t dpos;
unsigned char *win_buf;     /* the "sliding" window buffer. Max = 20 bits or 1MB */
unsigned char *pattern;     /* the "look-ahead" buffer. Max = 19 bits 512KB */
int win_cnt = 0, pat_cnt = 0, buf_cnt = 0;  /* some counters. */
int len_CODE = 0;     /* the transmitted length code. */

void copyright( void );
dpos_t search( unsigned char *w, unsigned char *p );
void put_codes( dpos_t *dpos );

void usage( void )
{
	fprintf(stderr, "\n Usage: lzuf621 [-cN -fM] infile outfile\n\n where N = nbits size (N = 12..20) of window buffer, default=17;");
	fprintf(stderr, "\n       M = bitsize of hash bucket search list (M = 1..12) default=9.");
	copyright();
	exit (0);
}

int main( int argc, char *argv[] )
{
	float ratio = 0.0;
	int i, in_argn = 1, out_argn = 2;
	file_stamp fstamp;
	
	clock_t start_time = clock();
	
	if ( argc == 5 ) {
		if ( argv[1][0] == '-' && tolower(argv[1][1]) == 'c' ) {
			num_POS_BITS = atoi(&argv[1][2]);
			if ( num_POS_BITS == 0 ) usage();
			else if ( num_POS_BITS < 12 ) num_POS_BITS = 12;
			else if ( num_POS_BITS > 21 ) num_POS_BITS = 21;
		}
		if ( argv[2][0] == '-' && tolower(argv[2][1]) == 'f') {
			far_LIST_BITS = atoi(&argv[2][2]);
			if ( far_LIST_BITS == 0 ) usage();
			else if ( far_LIST_BITS < 1 ) far_LIST_BITS = 1;
			else if ( far_LIST_BITS > 12 ) far_LIST_BITS = 12;
			far_LIST = 1<<far_LIST_BITS;
		}
		else usage();
	}
	if ( argc == 3 ){
		in_argn = 1;
		out_argn = 2;
	}
	else if ( argc == 5 ){
		in_argn = 3;
		out_argn = 4;
	} else usage();
	
	init_buffer_sizes( (1<<20) );
	
	if ( (gIN = fopen(argv[ in_argn ], "rb")) == NULL ) {
		fprintf(stderr, "\nError opening input file.");
		return 0;
	}
	if ( (pOUT = fopen(argv[ out_argn ], "wb")) == NULL ) {
		fprintf(stderr, "\nError opening output file.");
		return 0;
	}
	init_put_buffer();
	
	/* initialize */
	win_BUFSIZE  = 1<<num_POS_BITS;   /* must be a power of 2. */
	win_MASK     = win_BUFSIZE-1;
	hash_SHIFT   = num_POS_BITS-8;
	pat_BUFSIZE  = win_BUFSIZE>>1;    /* must be a power of 2. */
	pat_MASK     = pat_BUFSIZE-1;
	
	fprintf(stderr, "\n--[ A Lempel-Ziv Unary (LZUF) Coding Implementation ]--\n");
	fprintf(stderr, "\nWindow Buffer size used  = %15lu bytes", (ulong) win_BUFSIZE );
	fprintf(stderr, "\nLook-Ahead Buffer size   = %15lu bytes", (ulong) pat_BUFSIZE );
	fprintf(stderr, "\n\nName of input file : %s", argv[ in_argn ] );
	
	/* start Compressing to output file. */
	fprintf(stderr, "\n Compressing...");
	
	/* Write the FILE STAMP. */
	strcpy( fstamp.algorithm, "LZU" );
	fstamp.num_pos_bits = num_POS_BITS;
	fstamp.file_size = 0;  /* initial write. */
	fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
	nbytes_out = sizeof(file_stamp);
	
	/* allocate memory for the window and pattern buffers. */
	win_buf = (unsigned char *) malloc( sizeof(unsigned char) * win_BUFSIZE );
	if ( !win_buf ) {
		fprintf(stderr, "\nError alloc: window buffer.");
		goto halt_prog;
	}
	pattern = (unsigned char *) malloc( sizeof(unsigned char) * pat_BUFSIZE );
	if ( !pattern ) {
		fprintf(stderr, "\nError alloc: pattern buffer.");
		goto halt_prog;
	}
	
	/* set the sliding-window to all zero (0) values. */
	memset( win_buf, 0, win_BUFSIZE );
	
	/* initialize the table of pointers. */
	if ( !alloc_lzhash(win_BUFSIZE) ) goto halt_prog;
	
	/* initialize the search list. */
	for ( i = 0; i < win_BUFSIZE; i++ ) {
		lznext[i] = LZ_NULL;
		lzprev[i] = LZ_NULL;
		insert_lznode( hash(win_buf,i,win_MASK,win_MASK), i );
	}
	
	/* fill the pattern buffer. */
	buf_cnt = fread( pattern, 1, pat_BUFSIZE, gIN );
	
	/* initialize the input buffer. */
	init_get_buffer();
	nbytes_read = buf_cnt;
	
	/* initialize MTF list. */
	alloc_mtf(MTF_SIZE);
	
	/* compress */
	while ( buf_cnt > 0 ) {  /* look-ahead buffer not empty? */
		dpos = search( win_buf, pattern );

		/* encode prefix bits. */
		if ( dpos.len > MIN_LEN ) { /* more than MIN_LEN match? */
			put_ONE();            /* yes, send a 1 bit. */
		}
		else if ( dpos.len == MIN_LEN ) { /* exactly MIN_LEN matching characters? */
			put_ZERO();          /* yes, send a 0 bit. */
			put_ONE();           /* and a 1 bit. */
		}
		else {                  /* less than MIN_LEN matching characters. */
			put_ZERO();          /* send a 0 bit. */
			put_ZERO();          /* one more 0 bit to indicate a no match. */
		}

		/* encode window position or len codes. */
		put_codes( &dpos );
	}
	flush_put_buffer();
	fprintf(stderr, "complete.");
	
	/* get infile's size and get compression ratio. */
	nbytes_read = get_nbytes_read();
	
	/* re-Write the FILE STAMP. */
	rewind( pOUT );
	fstamp.file_size = nbytes_read; /* actual input file length. */
	fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
	
	fprintf(stderr, "\nName of output file: %s", argv[ out_argn ] );
	fprintf(stderr, "\nLength of input file     = %15llu bytes", nbytes_read );
	fprintf(stderr, "\nLength of output file    = %15llu bytes", nbytes_out );
	
	ratio = (((float) nbytes_read - (float) nbytes_out) /
		(float) nbytes_read ) * (float) 100;
	fprintf(stderr, "\nCompression ratio:         %15.2f %% in %3.2f secs.", ratio, 
		(double) (clock()-start_time) / CLOCKS_PER_SEC );

	copyright();

	halt_prog:
	
	free_put_buffer();
	free_get_buffer();
	free_lzhash();
	free_mtf_table();
	if ( win_buf ) free( win_buf );
	if ( pattern ) free( pattern );
	if ( gIN ) fclose( gIN );
	if ( pOUT ) fclose( pOUT );
	
	return 0;
}

void copyright( void )
{
	fprintf(stderr, "\n\n Gerald R. Tamayo (c) 2008-2023\n");
}

/*
This function searches the sliding window buffer for the largest
"string" stored in the pattern buffer.

The function uses an "array of pointers" to singly-linked
lists, which contain the various occurrences or "positions" of a
particular character in the sliding-window.

Note:

	We output 2 bits for a string of size MIN_LEN, so in terms of 
	the transmitted length code, MINIMUM_MATCH_LENGTH is actually 
	prev_LEN = (MIN_LEN+1) here, not MIN_LEN.
*/
dpos_t search( unsigned char *w, unsigned char *p )
{
	int i, j, k, m = 0, n = 0;
	dpos_t dpos = { 0, 0, 0 };
	
	/* point to start of lzhash[ index ] */
	i = lzhash[ hash(p,pat_cnt,pat_MASK,win_MASK) ];
	
	if ( buf_cnt > 1 ) while ( i != LZ_NULL ) {
		j = (pat_cnt+dpos.len) & pat_MASK;
		k = dpos.len;
		do {
			if ( p[j] != w[ (i+k) & win_MASK ] ) {
				goto skip_search;  /* allows fast search. */
			}
			if ( j-- == 0 ) j=pat_BUFSIZE-1;
		} while ( (--k) >= 0 );

		/* then match the rest of the "suffix" string from left to right. */
		j = (pat_cnt+dpos.len+1) & pat_MASK;
		k = dpos.len+1;
		if ( k < buf_cnt )
			while ( p[ j++ & pat_MASK ] == w[ (i+k) & win_MASK ]
				&& (++k) < buf_cnt ) ;

		/* greater than previous length, record it. */
		dpos.pos = i;
		dpos.len = k;
		
		/* maximum match, end the search. */
		if ( k == buf_cnt ) break;
		
		skip_search:
		
		if ( ++m == far_LIST ) break;

		/* point to next occurrence of this hash index. */
		i = lznext[i];
	}

	return dpos;
}

/*
Transmits a length/position pair of codes according
to the match length received.

When we receive a match length of 0, we quickly set the length
code to 1 (we have to "slide" through the window buffer at least
one character at a time).

Due to the algorithm, we only encode the match length if it is
greater than MIN_LEN. Next, a byte or a "position code" is
transmitted.

Then this function properly performs the "sliding" part by
copying the matched characters to the window buffer; note that
the linked list is also updated.

Finally, it "gets" characters from the input file according
to the number of matching characters.
*/
void put_codes( dpos_t *dpos )
{
	int i, k;
	
	/* the whole string match is encoded completely. (Oct. 19, 2008) */
	if ( dpos->len > MIN_LEN ) {
		/* suffix string length. */
		len_CODE = dpos->len - (MIN_LEN+1);
		#define MFOLD 2
		put_golomb( len_CODE, MFOLD );
	}
	
	/* encode position for match len >= MIN_LEN. */
	if ( dpos->len >= MIN_LEN ) {
		k = dpos->pos;
		put_nbits( k, num_POS_BITS );
	}
	else {
		dpos->len = 1;
		/* emit just the byte. */
		k = (unsigned char) pattern[pat_cnt];
		/* Implemented VL coding for better compression. (1/12/2010) */
		put_vlcode(mtf(k), 3);
	}
	
	/* ---- if its a match, then "slide" the buffer. ---- */
	if ( (k=win_cnt-(HASH_BYTES_N-1)) < 0 ) {
		/* record the left-most string index (k). */
		k = win_BUFSIZE+k;
	}
	
	/* remove the strings (i.e., positions) from the hash list. */
	for ( i = 0; i < (dpos->len+(HASH_BYTES_N-1)); i++ ) {
		delete_lznode( hash(win_buf,(k+i),win_MASK,win_MASK), (k+i) & win_MASK );
	}
	
	i = dpos->len;
	while ( i-- ) {
		/* write the character to the window buffer. */
		*(win_buf +((win_cnt+i) & (win_MASK)) ) =
			*(pattern + ((pat_cnt+i) & pat_MASK));
	}

	/* with the new characters, rehash at this position. */
	for ( i = 0; i < (dpos->len+(HASH_BYTES_N-1)); i++ ) {
		insert_lznode( hash(win_buf,(k+i),win_MASK,win_MASK), (k+i) & win_MASK );
	}
	
	/* get dpos.len bytes */
	for ( i = 0; i < dpos->len; i++ ){
		if( (k=gfgetc()) != EOF ) {
			*(pattern + ((pat_cnt+i) & pat_MASK)) =
				(uchar) k;
		}
		else break;
	}

	/* update counters. */
	buf_cnt -= (dpos->len-i);
	win_cnt = (win_cnt+dpos->len) & win_MASK;
	pat_cnt = (pat_cnt+dpos->len) & pat_MASK;
}
