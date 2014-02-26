/* tabToBigTab - Create a B+ tree index for the first field of a .tab file and write both index and file 
 * to a .bt file */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "sqlNum.h"
#include "localmem.h"
#include "bPlusTree.h"


int blockSize = 1000;

void usage()
/* Explain usage and exit. */
{
errAbort(
  "tabToBigTab - index a tab-sep file by the first field\n"
  "usage:\n"
  "   tabToBigTab input.tab output.bt\n"
  "where input.tab is at least one column\n"
  "options:\n"
  "   -blockSize=N (default %d) Size of block for index purposes\n"
  , blockSize
  );
}

static struct optionSpec options[] = {
   {"blockSize", OPTION_INT},
   {NULL, 0},
};

struct nameOffLen
/* Pair of a name and a 64-bit integer. */
    {
    struct nameOffLen *next;
    char *name;
    bits64 offset;
    bits64 len;
    };

int nameOffLenCmp(const void *va, const void *vb)
/* Compare to sort on name. */
{
const struct nameOffLen *a = *((struct nameOffLen **)va);
const struct nameOffLen *b = *((struct nameOffLen **)vb);
return strcmp(a->name, b->name);
}

void nameOffLenKey(const void *va, char *keyBuf)
/* Get key field. */
{
const struct nameOffLen *a = *((struct nameOffLen **)va);
strcpy(keyBuf, a->name);
}

void *nameOffLenVal(const void *va)
/* Get key field. */
{
const struct nameOffLen *a = *((struct nameOffLen **)va);
return (void*)(&a->offset);
}

static void writeFile(FILE *destF, char *inFname)
/* write inFname line by line to destF */
{
struct lineFile *lf = lineFileOpen(inFname, TRUE);

int lineSize;
char *line;
while (lineFileNext(lf, &line, &lineSize))
    mustWrite(destF, line, lineSize);
}
void writeBigTabHeader(FILE *f, bits64 indexOffset, bits64 dataOffset)
{
#define bigTabSig 0x8789F2EB // FIXME adapt one day
bits32 sig = bigTabSig;
bits16 version = 1;
bits32 uncompressBufSize = 0;
bits16 tableCount = 1;
bits64 allJoinerOffset = 0;
bits64 autoSqlOffset = 0;

writeOne(f, sig);
writeOne(f, version);
writeOne(f, uncompressBufSize);
writeOne(f, tableCount);
writeOne(f, allJoinerOffset);
writeOne(f, autoSqlOffset);
writeOne(f, indexOffset);
writeOne(f, dataOffset);
assert(ftell(f) == 44);
}

void bptMakeTabIndex(char *inFile, char *outIndex)
/* bptMakeStringToBits64 - Create a B+ tree index with string keys and 64-bit-integer values. 
 * In practice the 64-bit values are often offsets in a file.. */
{
/* Read inFile into a list in local memory. */
struct lm *lm = lmInit(0);
struct nameOffLen *el, *list = NULL;
struct lineFile *lf = lineFileOpen(inFile, TRUE);
char *row[2];
bits64 pos;
while (lineFileRow(lf, row))
    {
    pos = lf->bufOffsetInFile + lf->lineStart;
    lmAllocVar(lm, el);
    el->name = lmCloneString(lm, row[0]);
    el->offset = pos;
    el->len = lf->lineEnd - lf->lineStart;
    slAddHead(&list, el);
    }
lineFileClose(&lf);

int count = slCount(list);
if (count > 0)
    {
    /* Convert list into sorted array */
    struct nameOffLen **itemArray = NULL;
    AllocArray(itemArray, count);
    int i;
    for (i=0, el=list; i<count; i++, el = el->next)
        itemArray[i] = el;
    qsort(itemArray, count, sizeof(itemArray[0]), nameOffLenCmp);

    /* Figure out max size of name field. */
    int maxSize = 0;
    for (i=0; i<count; ++i)
        {
	int size = strlen(itemArray[i]->name);
	if (maxSize < size)
	    maxSize = size;
	}

    FILE *f = mustOpen(outIndex, "wb");
    writeBigTabHeader(f, 0, 0);

    // write the index
    bits64 indexStart = ftell(f);
    bptFileBulkIndexToOpenFile(itemArray, sizeof(itemArray[0]), count, blockSize, 
    	nameOffLenKey, maxSize, nameOffLenVal, sizeof(bits64) + sizeof(bits64), f);

    // write the data
    bits64 dataStart = ftell(f);
    writeFile(f, inFile);
    
    // write the header again, now with the real values for indexStart and dataStart
    rewind(f);
    writeBigTabHeader(f, indexStart, dataStart);
    carefulClose(&f);
    //bptFileCreate(itemArray, sizeof(itemArray[0]), count, blockSize,
    	//nameOffLenKey, maxSize, nameOffLenVal, sizeof(bits64) + sizeof(bits64), outIndex);
    }
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 3)
    usage();
blockSize = optionInt("blockSize", blockSize);
int minBlockSize = 2, maxBlockSize = (1L << 16) - 1;
if (blockSize < minBlockSize || blockSize > maxBlockSize)
    errAbort("Block size (%d) not in range, must be between %d and %d",
    	blockSize, minBlockSize, maxBlockSize);
bptMakeTabIndex(argv[1], argv[2]);
return 0;
}
