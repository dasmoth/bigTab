Header
------

    bits32  signature                 // 0x8789F2EB
    bits16  version                   // 2
    bits32  uncompressBufSize         //
    bits64  autoSqlOffset
    bits16  indexCount
    bits64  indexListOffset
    bits64  reserved

Index record  (matches bigbed)
------------------------------

    bits16    indexType
    bits16    fieldCount
    bits64    indexBptOffset
    bits32    reserved

        bits16  fieldId
        bits16  reserved

Index BPT
---------

Similar to bigbed, but with a pointer to the target record within the (uncompressed) block.
This limits block sizes to 2^32 bytes, but that doesn't seem like a major hardship.

      bits64    compressed payload block offset
      bits32    compressed payload block length
      bits32    index of record within *uncompressed* payload block.


Payload
-------

Split on record boundaries to make chunks <= uncompressBufSize
Compress with ZLib deflate (as per bigbed payload chunks).


Open questions
--------------

- Are multi-field indices overkill (bigBed tools don't currently seem to implement)
- Do we care about an uncompressed variant?
