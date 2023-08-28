Lempel-Ziv algorithms (LZ77):

(1)  lzh.c and lzhd.c       [2-byte hash];
(2)  lzhh.c and lzhhx.c     [2-byte hash plus Huffman] );
(3)  lzuf22.c and lzufd22.c [LZ77 + Variable-Length Codes];
(4)  lzuf62.c and lzufd62.c [lzuf22 not dependent on ftell(), and works on bigger files];
(5)  lzuf622.c [single file coder/decoder]:

In my tests, "lzuf622 -c17 -f2" is a little better than LZ4 high compression ("lz4 -9") in compression ratio at about the same compression speed on enwik8 and enwik9. That's testing only the 4 most recent offsets of the same hash. "Lzop -1" and "lzop -9" are better than "lz4 -1" and "lz4 -9" respectively but "lzop -9" is slower. "Lzuf622 -c17 -f3" is better than "lzop -9" but both lz4 and lzop decode way faster.

Notes:

For personal, academic, and research purposes only. Freely distributable.

-- Gerald Tamayo, BSIE(Mapua I.T.)
   Philippines
