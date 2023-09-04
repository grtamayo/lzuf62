Lempel-Ziv algorithms (LZ77/LZSS):

(1)  lzuf62.c and lzufd62.c [works on bigger files, optional sliding window size];
(2)  lzuf621.c [optional hash bucket search list size];
(3)  lzuf622.c [single file coder/decoder]:

In my tests, "lzuf622 -c17 -f2" is a little better than LZ4 high compression ("lz4 -9") in compression ratio at about the same compression speed on enwik8 and enwik9. That's testing only the 4 most recent offsets of the same hash. "Lzop -1" and "lzop -9" are better than "lz4 -1" and "lz4 -9" respectively but "lzop -9" is slower. "Lzuf622 -c17 -f3" is better than "lzop -9" but both lz4 and lzop decode way faster. Lzuf624 "-c17 -f3" is better than "lz4 -9" and lzuf624 "-c18 -f3" is better than "lzop -9". Lzuf624 decodes faster than lzuf622.

Notes:

For personal, academic, and research purposes only. Freely distributable.

-- Gerald Tamayo, BSIE(Mapua I.T.)
   Philippines
