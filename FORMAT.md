Header
------

    bits32  signature                 // 0x8789F2EB
    bits16  version                   // 2
    bits32  uncompressBufSize         //
    bits64  autoSqlOffset
    bits64  allJoinerOFfset           // What is this...?
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

As for bigBed.  Values are:

      bits64    compressed payload block offset
      bits64    payload block length


Payload
-------

Split on record boundaries to make chunks <= uncompressBufSize
Compress with ZLib deflate (as per bigbed payload chunks).


Open questions
--------------

- Are multi-field indices overkill (bigBed tools don't currently seem to implement)
- Do we care about an uncompressed variant?
- Is the bigBed pointer format optimal?  Seems like many wasted bits in the "length" field!
- Also, need to search for the target record within the uncompressed block.  Worth having an
  extra pointer?

Alternative pointer format might be:

      bits64   compressed payload block offset
      bits32   payload block length
      bits32   offset of target record within (uncompressed) payload.