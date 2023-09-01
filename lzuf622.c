/*
	---- A Lempel-Ziv Unary (LZUF) Coding Implementation ----

	Filename:      lzuf622.c
	Written by:    Gerald Tamayo, Oct. 22, 2008 (2/24/2022)
	
	Version 2:
		(2/27/2022) Optional bit size for sliding window implemented, BITS = 12..20, default = 17.
		(7/07/2023) Optional bitsize of "hash bucket" search list, BITS = 1..12, default = 9.
		(7/23/2023) Single file coder/decoder.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "utypes.h"
#include "gtbitio3.c"
#include "ucodes3.c"
#include "lzhash2.c"
#include "mtf.c"

enum {
	/* modes */
	COMPRESS,
	DECOMPRESS
};

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

#define HASH_BYTES_N      4

/* 4-byte hash */
#define hash(buf,pos,mask1,mask2) \
	(((buf[ (pos)&(mask1)]<<hash_SHIFT) \
	^(buf[((pos)+1)&(mask1)]<<7) \
	^(buf[((pos)+2)&(mask1)]<<4) \
	^(buf[((pos)+3)&(mask1)]))&(mask2))
	
typedef struct {
	char algorithm[8];
	int64_t file_size;
	int num_pos_bits;
} file_stamp;

typedef struct {
	unsigned int pos, len;
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
file_stamp fstamp;

void copyright( void );
void alloc_buffers( void );
void compress( unsigned char *w, unsigned char *p );
void decompress( unsigned char *w, unsigned char *p );
dpos_t search( unsigned char *w, unsigned char *p );
void put_codes( dpos_t *dpos );

void usage( void )
{
	fprintf(stderr, "\n Usage: lzuf622 [-c[N]] [-fM] [-d] infile outfile\n\n where c = encoding/compression.");
	fprintf(stderr, "\n       N = nbits size (N = 12..20) of window buffer, default=17;");
	fprintf(stderr, "\n       M = bitsize of hash bucket search list (M = 1..12) default=9.");
	fprintf(stderr, "\n       d = decoding.");
	copyright();
	exit (0);
}

int main( int argc, char *argv[] )
{
	float ratio = 0.0;
	int i, mode = -1, in_argn = 0, out_argn = 0, fcount = 0, n;
	
	clock_t start_time = clock();
	
	/* command-line handler */
	if ( argc < 3 || argc > 5 ) usage();
	else if ( argc == 3 ) mode = COMPRESS;
	n = 1;
	while ( n < argc ){
		if ( argv[n][0] == '-' ){
			switch( tolower(argv[n][1]) ){
				case 'c':
					if ( argv[n][2] != 0 ){
						num_POS_BITS = atoi(&argv[n][2]);
						if ( num_POS_BITS == 0 ) usage();
						else if ( num_POS_BITS < 12 ) num_POS_BITS = 12;
						else if ( num_POS_BITS > 20 ) num_POS_BITS = 20;
					}
					if ( mode == DECOMPRESS ) usage();
					else mode = COMPRESS;
					break;
				case 'f':
					far_LIST_BITS = atoi(&argv[n][2]);
					if ( far_LIST_BITS == 0 ) usage();
					else if ( far_LIST_BITS < 1 ) far_LIST_BITS = 1;
					else if ( far_LIST_BITS > 12 ) far_LIST_BITS = 12;
					far_LIST = 1<<far_LIST_BITS;
					if ( mode == DECOMPRESS ) usage();
					else mode = COMPRESS;
					break;
				case 'd':
					if ( argv[n][2] != 0 || mode == COMPRESS ) usage();
					mode = DECOMPRESS;
					break;
				default: usage();
			}
		}
		else {
			if ( in_argn == 0 ) in_argn = n;
			else if ( out_argn == 0 ) out_argn = n;
			if ( ++fcount == 3 ) usage();
		}
		++n;
	}
	if ( in_argn == 0 || out_argn == 0 ) usage();
	
	init_buffer_sizes( (1<<20) );
	
	if ( (gIN = fopen(argv[ in_argn ], "rb")) == NULL ) {
		fprintf(stderr, "\nError opening input file.");
		return 0;
	}
	if ( (pOUT = fopen(argv[ out_argn ], "wb")) == NULL ) {
		fprintf(stderr, "\nError opening output file." );
		return 0;
	}
	init_put_buffer();
	
	/* initialize MTF list. */
	alloc_mtf(MTF_SIZE);
	
	if ( mode == COMPRESS ){
		/* initialize */
		win_BUFSIZE  = 1<<num_POS_BITS;   /* must be a power of 2. */
		win_MASK     = win_BUFSIZE-1;
		hash_SHIFT   = num_POS_BITS-8;
		pat_BUFSIZE  = win_BUFSIZE>>1;    /* must be a power of 2. */
		pat_MASK     = pat_BUFSIZE-1;
		
		/* Write the FILE STAMP. */
		strcpy( fstamp.algorithm, "LZUF" );
		fstamp.num_pos_bits = num_POS_BITS;
		fstamp.file_size = 0;  /* initial write. */
		fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
		nbytes_out = sizeof(file_stamp);
		
		fprintf(stderr, "\n--[ A Lempel-Ziv Unary (LZUF) Coding Implementation ]--\n");
		fprintf(stderr, "\nWindow Buffer size used  = %15lu bytes", (ulong) win_BUFSIZE );
		fprintf(stderr, "\nLook-Ahead Buffer size   = %15lu bytes", (ulong) pat_BUFSIZE );
		fprintf(stderr, "\n\nName of input file : %s", argv[ in_argn ] );
		
		/* start Compressing to output file. */
		fprintf(stderr, "\n Compressing...");
		
		/* allocate memory for the window and pattern buffers. */
		alloc_buffers();
		
		/* initialize sliding-window. */
		memset( win_buf, 0, win_BUFSIZE );
		
		/* initialize the table of pointers. */
		if ( !alloc_lzhash(win_BUFSIZE) ) goto halt_prog;
		
		/* initialize the search list. */
		for ( i = 0; i < win_BUFSIZE; i++ ) {
			insert_lznode( hash(win_buf,i,win_MASK,win_MASK), i );
		}
		
		/* fill the pattern buffer. */
		buf_cnt = fread( pattern, 1, pat_BUFSIZE, gIN );
		
		/* initialize the input buffer. */
		init_get_buffer();
		nbytes_read = buf_cnt;
		
		compress( win_buf, pattern );
		fprintf(stderr, "complete.");
	}
	else if ( mode == DECOMPRESS ){
		fprintf(stderr, "\n Name of input  file : %s", argv[in_argn] );
		fprintf(stderr, "\n Name of output file : %s", argv[out_argn] );
		fprintf(stderr, "\n\n  Decompressing...");
		fread( &fstamp, sizeof(file_stamp), 1, gIN );
		init_get_buffer();
		nbytes_read = sizeof(file_stamp);
		
		/* initialize */
		num_POS_BITS = fstamp.num_pos_bits;
		win_BUFSIZE  = 1<<num_POS_BITS;   /* must be a power of 2. */
		win_MASK     = win_BUFSIZE-1;
		pat_BUFSIZE  = win_BUFSIZE>>1;    /* must be a power of 2. */
		
		/* allocate memory for the window and pattern buffers. */
		alloc_buffers();
		
		/* initialize sliding-window. */
		memset( win_buf, 0, win_BUFSIZE );
		
		decompress( win_buf, pattern );
		fprintf( stderr, "done.\n" );
	}
	flush_put_buffer();
	
	/* get infile's size and get compression ratio. */
	nbytes_read = get_nbytes_read();
	
	if ( mode == COMPRESS ){
		/* re-Write the FILE STAMP. */
		rewind( pOUT );
		fstamp.file_size = nbytes_read; /* actual input file length. */
		fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );
		fprintf(stderr, "\nName of output file: %s", argv[ out_argn ] );
		fprintf(stderr, "\nLength of input file     = %15llu bytes", nbytes_read );
		fprintf(stderr, "\nLength of output file    = %15llu bytes", nbytes_out );
		
		ratio = (((float) nbytes_read - (float) nbytes_out) /
			(float) nbytes_read ) * (float) 100;
		fprintf(stderr, "\nCompression ratio:         %15.2f %% ", ratio );
	}
	else if ( mode == DECOMPRESS ){
		fprintf(stderr, "  (%lld) -> (%lld)", nbytes_read, nbytes_out);
	}
	
	halt_prog:
	
	free_put_buffer();
	free_get_buffer();
	free_lzhash();
	free_mtf_table();
	if ( win_buf ) free( win_buf );
	if ( pattern ) free( pattern );
	fclose( gIN );
	fclose( pOUT );
	if ( mode == DECOMPRESS ) nbytes_read = nbytes_out;
	fprintf(stderr, " in %3.2f secs (@ %3.2f MB/s)",
		(double)(clock()-start_time) / CLOCKS_PER_SEC, (nbytes_read/1048576)/((double)(clock()-start_time)/ CLOCKS_PER_SEC) );
	copyright();
	return 0;
}

void copyright( void )
{
	fprintf(stderr, "\n\n Gerald R. Tamayo (c) 2008-2023\n");
}

void alloc_buffers( void )
{
	/* allocate memory for the window and pattern buffers. */
	win_buf = (unsigned char *) malloc( sizeof(unsigned char) * win_BUFSIZE );
	if ( !win_buf ) {
		fprintf(stderr, "\nError alloc: window buffer.");
		exit (0);
	}
	pattern = (unsigned char *) malloc( sizeof(unsigned char) * pat_BUFSIZE );
	if ( !pattern ) {
		fprintf(stderr, "\nError alloc: pattern buffer.");
		exit (0);
	}
}

void compress( unsigned char *w, unsigned char *p )
{
	/* compress */
	while ( buf_cnt > 0 ) {  /* look-ahead buffer not empty? */
		dpos = search( w, p );

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
}

void decompress( unsigned char *w, unsigned char *p )
{
	int i, k;
	int64_t fsize;
	
	fsize = fstamp.file_size;
	while ( fsize ) {
		if ( get_bit() == 1 ){
			/* get length. */
			for ( len_CODE = 0; get_bit(); len_CODE++ ) ;
			#define MFOLD    2
			len_CODE <<= MFOLD;
			len_CODE += get_nbits(MFOLD);
			
			/* get position. */
			dpos.pos = get_nbits( num_POS_BITS );
			dpos.len = len_CODE + (MIN_LEN+1);  /* actual length. */
			
			/* if its a match, then "slide" the window buffer. */
			i = dpos.len;
			while ( i-- ) {
				/* copy byte. */
				p[i] = w[ (dpos.pos+i) & win_MASK ];
			}
			i = dpos.len;
			while ( i-- ) {
				w[ (win_cnt+i) & win_MASK ] = p[ i ];
			}
			
			/* output string. */
			i = 0;
			while ( i < dpos.len ) {
				pfputc( p[i++] );
			}
			fsize -= dpos.len;
			win_cnt = (win_cnt + dpos.len) & win_MASK;
		}
		else {
			switch ( get_bit() ){
			case 0:
			
			/* get VL-coded byte and output it. */
			k = get_mtf_c(get_vlcode(3));
			pfputc( w[ (win_cnt) & win_MASK ] = k );
			if ( (++win_cnt) == win_BUFSIZE ) win_cnt = 0;
			--fsize;
			
			break;

			case 1:

			/* get position. */
			dpos.pos = get_nbits( num_POS_BITS );
			dpos.len = MIN_LEN;
			
			/* if its a match, then "slide" the window buffer. */
			i = dpos.len;
			while ( i-- ) {
				/* copy byte. */
				p[i] = w[ (dpos.pos+i) & win_MASK ];
			}
			i = dpos.len;
			while ( i-- ) {
				w[ (win_cnt+i) & win_MASK ] = p[ i ];
			}
			
			/* output string. */
			i = 0;
			while ( i < dpos.len ) {
				pfputc( p[i++] );
			}
			fsize -= dpos.len;
			win_cnt = (win_cnt + dpos.len) & win_MASK;
			
			break;
			}	/* end switch */
		}
	}
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
	dpos_t dpos = { 0, 0 };
	
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
	
	i = dpos->len;
	while ( i-- ) {
		/* write the character to the window buffer. */
		*(win_buf +((win_cnt+i) & (win_MASK)) ) =
			*(pattern + ((pat_cnt+i) & pat_MASK));
	}

	/* with the new characters, rehash at this position. */
	for ( i = 0; i < (dpos->len+(HASH_BYTES_N-1)); i++ ) {
		delete_lznode( hashp[(k+i) & win_MASK], (k+i) & win_MASK );
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
