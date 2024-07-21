/*******************************************************
                          cache.cc
********************************************************/

#include <stdlib.h>
#include <assert.h>
#include "cache.h"
#include <iomanip>
using namespace std;

Cache::Cache(int s, int a, int b)
{
   ulong i, j;
   reads = readMisses = writes = 0;
   writeMisses = writeBacks = currentCycle = 0;
   memTraffic = 0;
   busrdx = 0;
   // busrd = 0;
   busrdxCounter = 0;
   flush = 0;
   invalid = 0;
   busupdCounter = 0;
   size = (ulong)(s);
   lineSize = (ulong)(b);
   assoc = (ulong)(a);
   sets = (ulong)((s / b) / a);
   numLines = (ulong)(s / b);
   log2Sets = (ulong)(log2(sets));
   log2Blk = (ulong)(log2(b));

   //*******************//
   // initialize your counters here//
   //*******************//

   tagMask = 0;
   for (i = 0; i < log2Sets; i++)
   {
      tagMask <<= 1;
      tagMask |= 1;
   }

   /**create a two dimentional cache, sized as cache[sets][assoc]**/
   cache = new cacheLine *[sets];
   for (i = 0; i < sets; i++)
   {
      cache[i] = new cacheLine[assoc];
      for (j = 0; j < assoc; j++)
      {
         cache[i][j].invalidate();
      }
   }
}

/**you might add other parameters to Access()
since this function is an entry point
to the memory hierarchy (i.e. caches)**/
void Cache::Access(ulong addr, uchar op, ulong proc, ulong protocol, Cache **cacheArray)
{
   currentCycle++; /*per cache global counter to maintain LRU order
                     among cache ways, updated on every cache access*/


   if (op == 'w')
      writes++;
   else
      reads++;

   cacheLine *line = findLine(addr);
   if (line == NULL) /*miss*/
   {
      memTraffic++;
      if (op == 'w')
      {
         writeMisses++;
      }
      else
      {
         readMisses++;
      }
   }
   if (protocol == 0)
   {
      msi(addr, op, proc, cacheArray);
   }
   else
   {
      dragon(addr, op, proc, cacheArray);
   }
  
}

void Cache ::msi(ulong addr, uchar op, ulong proc, Cache **cacheArray)
{
   cacheLine *line = findLine(addr);
   if (line == NULL) /*miss*/
   {
      cacheLine *newline = fillLine(addr);
      if (op == 'w')
      {
         newline->setFlags(DIRTY);
         busrdx = true;
         busrdxCounter++;
      }
      else
      {
         newline->setFlags(CLEAN);
         busrd = true;
      }
   }
   else
   {
      /**since it's a hit, update LRU and update dirty flag**/
      updateLRU(line);
      if (op == 'w')
         line->setFlags(DIRTY);
   }

   for (ulong i = 0; i < 4; i++)
   {
      cacheLine *line1 = cacheArray[i]->findLine(addr);
      if (proc != i && line1 != NULL)
      {
         if (line1->getFlags() == DIRTY)
         {
            cacheArray[i]->memTraffic++;
            cacheArray[i]->flush++;
            cacheArray[i]->writeBacks++;
         }
         line1->invalidate();
         cacheArray[i]->invalid++;
      }
   }
}

void Cache ::dragon(ulong addr, uchar op, ulong proc, Cache **cacheArray)
{
   currentCycle++;
   bool copy = false;
   bool busrd = false;
   bool busUpdate= false;
       cacheLine *line = cacheArray[proc]->findLine(addr);
   for (ulong i = 0; i < 4; i++)
   {
      if (proc != i)
      {
         if (cacheArray[i]->findLine(addr) != NULL)
         {
            copy = true;
         }
      }
   }
   if (line == NULL) /*miss*/
   {
      cacheLine *newline = cacheArray[proc]->fillLine(addr);
      if (op == 'w')
      {
         if (copy)
         {
            newline->setFlags(SHARED_MODIFIED);
            busUpdate = true;
            busrd = true;
            busupdCounter++;
         }
         else
         {
            newline->setFlags(DIRTY);
            busrd = true;
         }
      }
      else
      {
         if (copy)
         {
            newline->setFlags(SHARED_CLEAN);
            busrd = true;
         }
         else
         {
            newline->setFlags(EXCLUSIVE);
            busrd = true;
         }

      }
   }
   else
   {
      cacheArray[proc]->updateLRU(line);

      if (op == 'w')
      {
         if (line->getFlags() == EXCLUSIVE)
         {
            line->setFlags(DIRTY);
         }
         if (copy)
         {
            if (line->getFlags() == SHARED_CLEAN || line->getFlags() == SHARED_MODIFIED)
            {
               // busupd=true;
               line->setFlags(SHARED_MODIFIED);
               busupdCounter++;
            }
            busUpdate = true;

         }
         else
         {
            if (line->getFlags() == SHARED_MODIFIED || line->getFlags() == SHARED_CLEAN)
            {
               line->setFlags(DIRTY);
               busUpdate = true;
               busupdCounter++;
            }
         }
      }
   }
   for (ulong i = 0; i < 4; i++)
   {
      cacheLine *line1 = cacheArray[i]->findLine(addr);
      if (proc != i && busrd)
      {
         if (line1 != NULL)
         {
            if (line1->getFlags() == EXCLUSIVE)
            {
               line1->setFlags(SHARED_CLEAN);
               cacheArray[i]->invalid++;
            }
            if (line1->getFlags() == SHARED_MODIFIED)
            {
               cacheArray[i]->flush++;
               cacheArray[i]->writeBacks++;
               cacheArray[i]->memTraffic++;
            }
            if (line1->getFlags() == DIRTY)
            {
               line1->setFlags(SHARED_MODIFIED);
               cacheArray[i]->invalid++;
               cacheArray[i]->flush++;
               cacheArray[i]->writeBacks++;
               cacheArray[i]->memTraffic++;
            }
         }
      }
      if (proc != i && busUpdate)
      {
         if (line1 != NULL)
         {
            if (line1->getFlags() == SHARED_MODIFIED)
            {
               line1->setFlags(SHARED_CLEAN);
            }
         }
      }
   }
}

/*look up line*/
cacheLine *Cache::findLine(ulong addr)
{
   ulong i, j, tag, pos;

   pos = assoc;
   tag = calcTag(addr);
   i = calcIndex(addr);

   for (j = 0; j < assoc; j++)
      if (cache[i][j].isValid())
      {
         if (cache[i][j].getTag() == tag)
         {
            pos = j;
            break;
         }
      }
   if (pos == assoc)
   {
      return NULL;
   }
   else
   {
      return &(cache[i][pos]);
   }
}

/*upgrade LRU line to be MRU line*/
void Cache::updateLRU(cacheLine *line)
{
   line->setSeq(currentCycle);
}

/*return an invalid line as LRU, if any, otherwise return LRU line*/
cacheLine *Cache::getLRU(ulong addr)
{
   ulong i, j, victim, min;

   victim = assoc;
   min = currentCycle;
   i = calcIndex(addr);

   for (j = 0; j < assoc; j++)
   {
      if (cache[i][j].isValid() == 0)
      {
         return &(cache[i][j]);
      }
   }

   for (j = 0; j < assoc; j++)
   {
      if (cache[i][j].getSeq() <= min)
      {
         victim = j;
         min = cache[i][j].getSeq();
      }
   }

   assert(victim != assoc);

   return &(cache[i][victim]);
}

/*find a victim, move it to MRU position*/
cacheLine *Cache::findLineToReplace(ulong addr)
{
   cacheLine *victim = getLRU(addr);
   updateLRU(victim);

   return (victim);
}

/*allocate a new line*/
cacheLine *Cache::fillLine(ulong addr)
{
   ulong tag;

   cacheLine *victim = findLineToReplace(addr);
   assert(victim != 0);

   if (victim->getFlags() == DIRTY || victim->getFlags() == SHARED_MODIFIED)
   {
      writeBack(addr);
      memTraffic++;
   }

   tag = calcTag(addr);
   victim->setTag(tag);
   victim->setFlags(VALID);
   /**note that this cache line has been already
      upgraded to MRU in the previous function (findLineToReplace)**/

   return victim;
}

void Cache::printStats(ulong protocol, ulong i)
{
   cout << "============ Simulation results (Cache " << i << ") ============\n";
   /****follow the ouput file format**************/
   {
      // printf("===== Simulation results      =====\n");
      /****print out the rest of statistics here.****/
      /****follow the ouput file format**************/
      cout << "01. number of reads:                            " << getReads() << endl;
      cout << "02. number of read misses:                      " << getRM() << endl;
      cout << "03. number of writes:                           " << getWrites() << endl;
      cout << "04. number of write misses:                     " << getWM() << endl;
      cout << "05. total miss rate:                            " << setprecision(2) << fixed << ((((double)(getWM() + getRM())) / ((double)(getReads() + getWrites()))) * 100) << "%" << endl;
      cout << "06. number of writebacks:                       " << getWB() << endl;
      cout << "07. number of memory transactions:              " << memTraffic << endl;
      if (protocol == 0)
      {
         cout << "08. number of invalidations:                    " << invalid << endl;
         cout << "09. number of flushes:                          " << flush << endl;
         cout << "10. number of BusRdX:                           " << busrdxCounter << endl;
      }
      else
      {
         cout << "08. number of interventions:                    " << invalid << endl;
         cout << "09. number of flushes:                          " << flush << endl;
         cout << "10. number of Bus Transactions(BusUpd):         " << busupdCounter << endl;
      }
   }
}

