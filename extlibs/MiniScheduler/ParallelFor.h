
#ifndef _PARALLEL_FOR_H_
#define _PARALLEL_FOR_H_

#include "eastl/delegate.h"

class WorkerThread;

//________________________________________________________________________________
class ParallelRange
{
public:
   ParallelRange() {}
   
   ParallelRange( const ParallelRange& R )
   {
      mBegin = R.mBegin;
      mEnd = R.mEnd;
      mGranularity = R.mGranularity;
   }

   ParallelRange( int _begin, int _end ) {
      mBegin = _begin;
      mEnd = _end;
      mGranularity = 1;
   }

   ParallelRange( int _begin, int _end, int _granularity ) {
      mBegin = _begin;
      mEnd = _end;
      mGranularity = _granularity;
   }

public:
   int mBegin;
   int mEnd;
   int mGranularity;
};

////________________________________________________________________________________
//class ParallelForTask
//{
//public:
//   virtual bool doRange( WorkerThread* pThread,
//                        const ParallelRange& Range ) const =0;
//};
//________________________________________________________________________________
bool ParallelFor( const eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> callback,
                 //const ParallelForTask* pTask,
                 const ParallelRange& Range );

bool ParallelFor( WorkerThread* pThread,
                 eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> callback,
                 //const ParallelForTask* pTask,
                 const ParallelRange& Range );

#endif // _PARALLEL_FOR_H_
