#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//Function definitions
void initialize();
void store(int address, int numBytes, char* data);
void load(int address, int numBytes);
int getSetIndex(int address);
int getTag(int address);
int getBlockOffset(int address);
int inCache(int address);
void writeToCache(int address, int numBytes, char* data, int cellIndex);
void writeToMemory(int address, int numBytes, char* data);
void readFromMemory(int address, int numBytes, char* data);
void readFromCache(int address, int numBytes, char* data, int cellIndex);
int mylog2(int num);
void appendZero(char* data, int length);


int cacheSize;
int associa;
int writeMode; //0 write through 1 write back
int blockSize;
int setNum;
int blockOffsetBitNum;
int setIndexBitNum;
int tagBitNum;
int operationNum;

struct cacheCell //Define a cache memory cell
{
	int tag;
	bool dirty;
	bool valid;
	int age;
	char data[64][2]; //max 64 Byte block size
};

char mainMemory[2][1<<24]; //24 bits address space mainMemory
struct cacheCell ** cache; //cache 2-D array of cacheCells

int main(int argc, char** argv)
{
	//Process commandline arguments
	char fname[30];
	strcpy(fname, argv[1]);
	cacheSize = atoi(argv[2]);
	associa = atoi(argv[3]);
	writeMode = strcmp(argv[4], "wt")==0 ? 0:1;
	blockSize = atoi(argv[5]);
	setNum = (cacheSize<<10)/blockSize/associa;
	blockOffsetBitNum = mylog2(blockSize);
	setIndexBitNum = mylog2(setNum);
	tagBitNum = 24 - setIndexBitNum - blockOffsetBitNum;
	//Initialization of memory and cache
	initialize();
	
	//printf("%s %d %d %d %d %d %d %d\n", fname, cacheSize, associa, writeMode, blockSize, setNum, blockOffsetBitNum, setIndexBitNum);
	
	FILE* file = fopen(fname, "r");
	char ins[7];
	while(fscanf(file, "%s", ins)!=EOF)
	{
		if(strcmp(ins, "store")==0)
		{
			int address;
			int numBytes;
			char data[200];
			fscanf(file, "%x %d %s", &address, &numBytes, data);
			int strlength;
			if((strlength=strlen(data))%2==1) appendZero(data, strlength); //if string length is even, need to append a 0 to the front
			operationNum++;
			store(address, numBytes, data);
			//printf("%s %d bytes of Data %s at address %d\n", ins, numBytes, data, address);
			//printf("Set Index: %d \n", getSetIndex(address));
		}
		else if(strcmp(ins, "load")==0)
		{
			int address;
			int numBytes;
			fscanf(file, "%x %d", &address, &numBytes);
			operationNum++;
			load(address, numBytes);
			//printf("%s %d bytes of Data at address %d\n", ins, numBytes, address);
			//printf("Set Index: %d \n", getSetIndex(address));
		}
	}
	return 0;
}

void initialize()
{
	memset(mainMemory[0], '0', 1<<24); //Clear mainMemory
	memset(mainMemory[1], '0', 1<<24);
	cache = (struct cacheCell**) calloc(setNum, sizeof(struct cacheCell*)); //Allocate set space
	int i;
	for(i=0;i<setNum;i++) //For each set, allocate associative cacheCells, initialized to zero
	{
		struct cacheCell* oneSet = (struct cacheCell*) calloc(associa, sizeof(struct cacheCell));
		cache[i] = oneSet;
	}
	operationNum = 0;
}

void appendZero(char* data, int length)
{
	int i;
	for(i=length;i>=0;i--)
	{
		data[i+1] = data[i];
	}
	data[0] = '0';
}

int mylog2(int num)
{
	int count = 0;
	while(num>>1!=0)
	{
		num>>=1;
		count++;
	}
	return count;
}

void store(int address, int numBytes, char* data)
{
	if(writeMode==0) //Write Through
	{
		int cellIndex;
		if((cellIndex = inCache(address))!=-1) //hit [if hit, then only need to update the portion of the block]
		{
			writeToMemory(address, numBytes, data);
			writeToCache(address, numBytes, data, cellIndex);
			printf("store %#08x hit\n", address);
		}
		else //miss [if miss, only write the data into memory: write no allocate]
		{
			writeToMemory(address, numBytes, data);
			printf("store %#08x miss\n", address);
		}
	}
	else //Write Back
	{
		int cellIndex;
		if((cellIndex = inCache(address))!=-1) //hit [if hit, only need to update the cache and mark the block as dirty]
		{
			writeToCache(address, numBytes, data, cellIndex);
			printf("store %#08x hit\n", address);
		}
		else //miss [if miss, need to read block from memory, write block to cache and update the cache]
		{
			char blockData[blockSize*2+1];
			int blockStartAddress = address & (~(blockSize-1)); //read the whole block from memory into cache
			readFromMemory(blockStartAddress, blockSize, blockData);
			writeToCache(blockStartAddress, blockSize, blockData, -1);
			cellIndex = inCache(address);
			writeToCache(address, numBytes, data, cellIndex);
			printf("store %#08x miss\n", address);
		}
	}
}

void load(int address, int numBytes)
{
	int cellIndex;
	if((cellIndex = inCache(address))!=-1) //hit [if hit, the data must be valid]
	{
		char data[numBytes*2+1];
		readFromCache(address, numBytes, data, cellIndex);
		printf("load %#08x hit %s\n", address, data);
	}
	else //miss [if miss, need to read from the memory and write to cache]
	{
		char data[blockSize*2+1];
		readFromMemory(address, numBytes, data); //read data from memory
		printf("load %#08x miss %s\n", address, data);
		int blockStartAddress = address & (~(blockSize-1)); //read the whole block from memory into cache
		readFromMemory(blockStartAddress, blockSize, data);
		writeToCache(blockStartAddress, blockSize, data, -1);
	}
}

int getSetIndex(int address)
{
	return (address>>blockOffsetBitNum)&(setNum-1);
}

int getTag(int address)
{
	return address>>blockOffsetBitNum>>setIndexBitNum;
}

int getBlockOffset(int address)
{
	return address&(blockSize-1);
}

int inCache(int address) //If data in memory and is valid, return cellIndex, otherwise return -1
{
	//printf("%d\n", address);
	int setIndex = getSetIndex(address);
	int tag = getTag(address);
	struct cacheCell *currentSet = cache[setIndex];
	int i;
	for(i=0;i<associa;i++)
	{
		if(currentSet[i].tag==tag && currentSet[i].valid) return i;
	}
	return -1;
}

void writeToMemory(int address, int numBytes, char* data) //Assuming data is of even length
{
	int i;
	for(i=0;i<numBytes;i++)
	{
		mainMemory[0][address+i] = data[2*i];
		mainMemory[1][address+i] = data[2*i+1];
	}
}

void writeToCache(int address, int numBytes, char* data, int cellIndex) //Assuming data is of even length
{
	int setIndex = getSetIndex(address);
	int offset = getBlockOffset(address);
	int tag = getTag(address);
	struct cacheCell *currentSet = cache[setIndex];
	
	if(cellIndex==-1) //LRU replacement
	{
		int LRUIndex = 0;
		int LRUAge = currentSet[0].age;
		int i;
		for(i=0;i<associa;i++)
		{
			if(!currentSet[i].valid)
			{
				LRUIndex = i;
				break;
			}
			if(LRUAge<currentSet[i].age)
			{
				LRUIndex = i;
				LRUAge = currentSet[i].age;
			}
		}
		cellIndex = LRUIndex;
		if(currentSet[cellIndex].dirty) //If dirty, need to update to main memory
		{
			char blockData[blockSize*2+1];
			int blockStartAddress = (currentSet[cellIndex].tag<<(blockOffsetBitNum+setIndexBitNum)) + (setIndex<<blockOffsetBitNum);
			readFromCache(blockStartAddress, blockSize, blockData, cellIndex);
			writeToMemory(blockStartAddress, blockSize, blockData);
		}
	}

	int i;
	for(i=0;i<numBytes;i++)
	{
		currentSet[cellIndex].data[offset+i][0] = data[2*i];
		currentSet[cellIndex].data[offset+i][1] = data[2*i+1];
	}
	currentSet[cellIndex].age = operationNum;
	currentSet[cellIndex].valid = true;
	currentSet[cellIndex].dirty = true;
	currentSet[cellIndex].tag = tag;
}

void readFromCache(int address, int numBytes, char* data, int cellIndex)
{
	int setIndex = getSetIndex(address);
	int offset = getBlockOffset(address);
	struct cacheCell *currentSet = cache[setIndex];
	int i;
	for(i=0;i<numBytes;i++)
	{
		data[2*i] = currentSet[cellIndex].data[offset+i][0];
		data[2*i+1] = currentSet[cellIndex].data[offset+i][1];
	}
	currentSet[cellIndex].age = operationNum;
	data[numBytes*2] = '\0';
}

void readFromMemory(int address, int numBytes, char* data)
{
	int i;
	for(i=0;i<numBytes;i++)
	{
		data[2*i] = mainMemory[0][address+i];
		data[2*i+1] = mainMemory[1][address+i];
	}
	data[numBytes*2] = '\0';
}