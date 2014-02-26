/* tabToBigTab - Create a B+ tree index for the first field of a .tab file and write both index and file 
 * to a .bt file */
#include "common.h"
#include "linefile.h"
#include "hash.h"
#include "options.h"
#include "sqlNum.h"
#include "localmem.h"
#include "bPlusTree.h"
#include "obscure.h"
#include "asParse.h"
#include "zlibFace.h"

#include <stdio.h>

int blockSize = 1000;
char *asFile = NULL;
char *asText = NULL;
char *indexCols = NULL;

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
  "   -as=fields.as - columns in AutoSql format\n"
  "   -index=fieldList - If set, make an index on each field in a comma-separated list.\n"
  "                      If omitted, just index the first column.\n"
  , blockSize
  );
}

static struct optionSpec options[] = {
   {"blockSize", OPTION_INT},
   {"as", OPTION_STRING},
   {"index", OPTION_STRING},
   {NULL, 0},
};

struct nameBigTabPtr
/* Pair of a name and a 64-bit integer. */
    {
    struct nameBigTabPtr *next;
    char *name;
    bits64 blockOffset;
    bits32 blockLen;
    bits32 recordOffset;
    };

int nameBigTabPtrCmp(const void *va, const void *vb)
/* Compare to sort on name. */
{
const struct nameBigTabPtr *a = *((struct nameBigTabPtr **)va);
const struct nameBigTabPtr *b = *((struct nameBigTabPtr **)vb);
return strcmp(a->name, b->name);
}

void nameBigTabPtrKey(const void *va, char *keyBuf)
/* Get key field. */
{
const struct nameBigTabPtr *a = *((struct nameBigTabPtr **)va);
strcpy(keyBuf, a->name);
}

void *nameBigTabPtrVal(const void *va)
/* Get key field. */
{
const struct nameBigTabPtr *a = *((struct nameBigTabPtr **)va);
return (void*)(&a->blockOffset);
}

void writeBigTabHeader(FILE *f, bits32 uncompressBufSize, bits64 autoSqlOffset, bits16 indexCount, bits64 indexListOffset)
{
#define bigTabSig 0x8789F2EB // FIXME adapt one day
bits32 sig = bigTabSig;
bits16 version = 2;
bits64 reserved = 0;

writeOne(f, sig);
writeOne(f, version);
writeOne(f, uncompressBufSize);
writeOne(f, autoSqlOffset);
writeOne(f, indexCount);
writeOne(f, indexListOffset);
writeOne(f, reserved);
assert(ftell(f) == 36);
}

static struct nameBigTabPtr* writeBigTabBlocks(FILE* f, struct lineFile *lf, struct asObject *as, bits16 fieldCount, int maxChunkSize, int fieldId) {
struct dyString *stream = dyStringNew(0);
char *line, *storedLine, *row[fieldCount + 1];
boolean atEnd = FALSE;
struct nameBigTabPtr* el, *list = NULL;
bits64 blockStartOffset = ftell(f);

for (;;) {
    if (lineFileNextReal(lf, &line)) {
        storedLine = cloneString(line);
        int wordCount = chopTabs(line, row);
        lineFileExpectWords(lf, fieldCount, wordCount);
    } else {
        atEnd = TRUE;
    }

    if (atEnd || (stream->stringSize + strlen(line) + 1) > maxChunkSize) {
        size_t maxCompSize = zCompBufSize(stream->stringSize);

        static int compBufSize = 0;
        static char *compBuf = NULL;

        if (compBufSize < maxCompSize) {
            freez(&compBuf);
            compBufSize = maxCompSize;
            compBuf = needLargeMem(compBufSize);
        }

        int compSize = zCompress(stream->string, stream->stringSize, compBuf, maxCompSize);
        mustWrite(f, compBuf, compSize);
        dyStringClear(stream);

        el = list;
        while (el != NULL && el->blockLen == 0) {
            el->blockLen = compSize;
            el = el->next;
        }

        blockStartOffset = ftell(f);
    }

    if (atEnd)
        break;

    AllocVar(el);
    el->name = cloneString(row[fieldId]);
    el->blockOffset = blockStartOffset;
    el->blockLen = 0;  // Gets fixed up after the block is closed.
    el->recordOffset = stream->stringSize;
    slAddHead(&list, el);

    dyStringAppend(stream, storedLine);
    dyStringAppendC(stream, '\n');
    freeMem(storedLine);
}

return list;
}

void writeIndexList(FILE *f, bits16 fieldId, bits64 offset) {
    bits16 indexType = 0;
    bits16 fieldCount = 1;
    bits32 reserved1 = 0;
    bits16 reserved2 = 0;

    writeOne(f, indexType);
    writeOne(f, fieldCount);
    writeOne(f, offset);
    writeOne(f, reserved1);

    writeOne(f, fieldId);
    writeOne(f, reserved2);
}

void tabToBigTab(char *inFile, char *outIndex)
/* tabToBigTab - Create a bigTab file*/
{
int bufSize = 32000;
int fieldId = 0;   // Hardwired for now, should be controlled by -index option

/* Read and check autoSql */
struct asObject *as = asParseText(asText);

/* Create output and write dummy header */
FILE *f = mustOpen(outIndex, "wb");
writeBigTabHeader(f, bufSize, 0, 1, 0);

/* Write out autoSql string */
bits64 autoSqlOffset = ftell(f);
mustWrite(f, asText, strlen(asText) + 1);

/* Chunk and compress the payload, record offsets*/
struct lineFile *lf = lineFileOpen(inFile, TRUE);
struct nameBigTabPtr *list = writeBigTabBlocks(f, lf, as, slCount(as->columnList), bufSize, fieldId);
lineFileClose(&lf);


/* Write BPT */
int count = slCount(list);
struct nameBigTabPtr **itemArray = NULL;
AllocArray(itemArray, count);
int i;
struct nameBigTabPtr *el;
for (i=0, el=list; i < count; i++, el=el->next)
    itemArray[i] = el;
qsort(itemArray, count, sizeof(itemArray[0]), nameBigTabPtrCmp);


int maxSize = 0;
for (i=0; i<count; ++i) {
    int size = strlen(itemArray[i]->name);
    if (maxSize < size)
        maxSize = size;
}

/* Write index header with dummy offset */
bits64 indexListOffset = ftell(f);
writeIndexList(f, fieldId, 0);

bits64 indexStart = ftell(f);
bptFileBulkIndexToOpenFile(itemArray, sizeof(itemArray[0]), count, blockSize, 
    nameBigTabPtrKey, maxSize, nameBigTabPtrVal, sizeof(bits64) + sizeof(bits64), f);

fseek(f, indexListOffset, SEEK_SET);
writeIndexList(f, fieldId, indexStart);

/* Write final header */
rewind(f);
writeBigTabHeader(f, bufSize, autoSqlOffset, 1, indexListOffset);
carefulClose(&f);
}

int main(int argc, char *argv[])
/* Process command line. */
{
optionInit(&argc, argv, options);
if (argc != 3)
    usage();
blockSize = optionInt("blockSize", blockSize);
asFile = optionVal("as", asFile);
indexCols = optionVal("index", indexCols);
int minBlockSize = 2, maxBlockSize = (1L << 16) - 1;
if (blockSize < minBlockSize || blockSize > maxBlockSize)
    errAbort("Block size (%d) not in range, must be between %d and %d",
    	blockSize, minBlockSize, maxBlockSize);
if (asFile)
    readInGulp(asFile, &asText, NULL);
else
    errAbort("Currently need -as");

tabToBigTab(argv[1], argv[2]);
return 0;
}
