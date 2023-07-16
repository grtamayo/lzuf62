/*
	Filename:   lzufd622.c (Oct. 22, 2008) .(4/11/2010)(2/24/2022)
	Encoder:    lzuf62.c
	
	Decompression in LZ77/LZSS is faster since you just have to extract
	the bytes from the window buffer using the pos and len variables.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "utypes.h"
#include "gtbitio3.c"
#include "ucodes3.c"
#include "mtf.c"

#define LTCB              17              /* 12..20 tested working */
#ifdef LTCB 
    #define NUM_POS_BITS LTCB
#else 
    #define NUM_POS_BITS  15
#endif

#define MIN_LEN           4               /* minimum string size >= 2 */
#define MTF_SIZE        256

typedef struct {
	char algorithm[4];
	int64_t file_size;
	int num_pos_bits;
} file_stamp; 

typedef struct {
	unsigned int pos, len;
} dpos_t;

unsigned int num_POS_BITS = NUM_POS_BITS; /* default */
unsigned int win_BUFSIZE  = 1<<NUM_POS_BITS;
unsigned int win_MASK;
unsigned int pat_BUFSIZE;   /* must be a power of 2. */

dpos_t dpos;
uchar *win_buf;  /* the "sliding window" buffer. */
uchar *pattern;  /* the "look-ahead" buffer (LAB). */
int win_cnt = 0, len_CODE = 0;

void copyright( void );

int main( int argc, char *argv[] )
{	
	int64_t fsize = 0;
	unsigned int i, k;
	unsigned char *p;
	file_stamp fstamp;

	if ( argc != 3 ) {
		fprintf(stderr, "\n Usage: lzufd622 infile outfile");
		copyright();
		return 0;
	}
	
	clock_t start_time = clock();
	
	init_buffer_sizes( (1 << 20) );
	
	if ( (gIN = fopen(argv[1], "rb")) == NULL ) {
		fprintf(stderr, "\nError opening input file.");
		return 0;
	}
	fread( &fstamp, sizeof(file_stamp), 1, gIN );
	init_get_buffer();
	
	if ( (pOUT = fopen(argv[2], "wb")) == NULL ) {
		fprintf(stderr, "\nError opening output file.");
		goto halt_prog;
	}
	init_put_buffer();
	
	fprintf(stderr, "\n Name of input  file : %s", argv[1] );
	fprintf(stderr, "\n Name of output file : %s", argv[2] );

	fprintf(stderr, "\n\n  Decompressing...");
	
	/* initialize */
	num_POS_BITS = fstamp.num_pos_bits;
	win_BUFSIZE  = 1<<num_POS_BITS;   /* must be a power of 2. */
	win_MASK     = win_BUFSIZE-1;
	pat_BUFSIZE  = win_BUFSIZE>>1;    /* must be a power of 2. */
	
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
	p = pattern;
	
	/* initialize sliding-window. */
	memset( win_buf, 0, win_BUFSIZE );
	alloc_mtf(MTF_SIZE);
	
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
				p[i] = win_buf[ (dpos.pos+i) & win_MASK ];
			}
			i = dpos.len;
			while ( i-- ) {
				win_buf[ (win_cnt+i) & win_MASK ] = p[ i ];
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
			pfputc( win_buf[ (win_cnt) & win_MASK ] = k );
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
				p[i] = win_buf[ (dpos.pos+i) & win_MASK ];
			}
			i = dpos.len;
			while ( i-- ) {
				win_buf[ (win_cnt+i) & win_MASK ] = p[ i ];
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
	flush_put_buffer();
	
	fprintf(stderr, "done, in %3.2f secs.",
		(double)(clock()-start_time) / CLOCKS_PER_SEC);
	
	copyright();
	
	halt_prog:
	
	free_get_buffer();
	free_put_buffer();
	free_mtf_table();
	if ( win_buf ) free( win_buf );
	if ( pattern ) free( pattern );
	if ( gIN ) fclose( gIN );
	if ( pOUT ) fclose( pOUT );
	return 0;
}

void copyright( void )
{
	fprintf(stderr, "\n\n Written by: Gerald Tamayo, 2008-2023\n");
}
